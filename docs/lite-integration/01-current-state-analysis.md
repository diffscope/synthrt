# 现状分析与痛点清单

日期: 2026-07-25

---

## 1. 当前架构（v2 组件式 facade）

lite 通过 [SynthrtEngine.h](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SynthrtEngine.h) 直接组合 5 个 synthrt 组件，外加 2 个自有包装层：

```
SynthrtEngine (singleton, QObject)
  ├── PackageCatalog m_catalog             [lite 自有] 包装 VoicebankScanner
  │     └── ds::bank::VoicebankScanner      [synthrt]
  ├── srt::g2p::LanguageService m_langSvc  [synthrt]   调 deprecated initialize()
  ├── srt::core::Runtime m_runtime         [synthrt]
  ├── std::set<path> m_loadedPackageDirs   [lite 自有] 非版本感知
  ├── std::map<SingerIdentifier, shared_ptr<SingerModelSession>> m_singerSessions
  │                                          [lite 自有] SingerIdentifier → ModelSet 映射
  │     └── SingerModelSession              [lite 自有] 包装 ModelSet + 互斥锁
  │           └── ds::infer::ModelSet       [synthrt]
  └── srt::g2p::Manager (process singleton) G2P ONNX driver 由 lite 注册
```

**消费方**（23 个文件）：
- `InferEngine` / `InferAcousticTask` / `InferDurationTask` / `InferPitchTask` / `InferVarianceTask` / `GetPhonemeNameTask` / `GetPronunciationTask` — 推理主链路
- `PackageManager` — 包列表 UI
- `G2pService` / `LyricDialog` — 歌词填充
- `ExtractPitchTask` / `ExtractMidiTask` — 音频特征提取（与 voicebank 无关，直接使用 Runtime）
- `ProjectPackageResolver` / `ClipsInfo` / `TrackControlView` / `ClipEditorToolBarView` 等 — UI 状态

---

## 2. 痛点清单

### P1: 与 synthrt v3 入口重复（违反 ARCH-03/ARCH-04）

synthrt 在 [docs/modules/overview.md](file:///d:/projects/synthrt/docs/modules/overview.md) §3.1 明确推荐 `ds::session::VoicebankSession(SessionResources)` 为"宿主层唯一入口"。lite 却重复实现了其内部机制：

| VoicebankSession 提供 | lite 重复实现 | 重复点 |
|---|---|---|
| `VoicebankSnapshot`（含 generation / catalogFingerprint / languageFingerprint / manifests / packages / singers） | `PackageCatalog::Snapshot` | 字段几乎一一对应 |
| `RefreshResult{changed, coalesced, diagnostics, changes, updatesAvailable}` | `PackageCatalog::Candidate::unchanged()` + `validate()` | 仅做部分检查 |
| `ModelSetHandle::isStale()` + `start()` 返回 `StaleModelSet` | 无；lite 用 `m_singerSessions` 缓存 invalidate 全表 | 缺失 stale 协议 |
| `loadVoicebank(packageId, version)` + `LoadedVoicebankInfo` | `m_loadedPackageDirs` (set<path>) | 非版本感知，违反 project_memory 约束 |
| `capabilitySummary(SingerRef)` 三态 + diagnostics | `findSinger` 自实现 `PackageVersionConflict` | 错误码与语义不一致 |
| `ensureLanguageReady` / `ensureModelSet` 显式错误分类 | `acquireSingerSession` 返回 nullptr + qCritical | 错误吞没，违反 ROBUST-05 |

### P2: 刷新阻塞（违反 K-04 / D-30）

[SynthrtEngine.cpp#L537-L543](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SynthrtEngine.cpp) 在 `m_loadedPackageDirs` 非空时直接返回 `PackageScanAfterInitialize` 错误：

```cpp
if (!m_loadedPackageDirs.empty() &&
    candidate->snapshot().catalogFingerprint != accepted->catalogFingerprint) {
    return Error(ErrorCode::PackageScanAfterInitialize,
                 "Package refresh changes voicebank metadata after packages were loaded...");
}
```

语义问题：用户安装新声库或更新已有声库后必须**重启应用**。VoicebankSession 设计的 `StaleModelSet` 机制本可让运行中任务完成、新任务用新快照重试一次（D-30）。

### P3: 版本感知缺失（违反 V3-01 / V3-10）

[SynthrtEngine.cpp#L383](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SynthrtEngine.cpp)：

```cpp
m_langSvc.initialize(g2pPluginPaths, g2pPathList, pkgDirs);  // pkgDirs 是 unordered_map<id, path>
```

调用的是 [LanguageService.h#L134](file:///d:/projects/synthrt/include/synthrt/G2P/LanguageService.h) 的 `initialize()` —— **convenience wrapper 内部委托 deprecated `initializeMetadata(map)`**。

后果：
- `pkgDirs` 用 `unordered_map<packageId, path>`，多版本同 packageId 在 map 中互相覆盖（`emplace` 不覆盖但插入失败，首个版本胜出，其余静默丢失）
- `resolveLanguageRoute(packageId, singerId, lang)` 调 deprecated 3-arg 重载 → 空 version + 多版本注册 → `G2pVersionAmbiguous`，但 lite 当前不解析该错误码
- S2P 缓存键无 version 维度，多版本同 packageId 共用同一缓存槽，可能返回错误音素表

### P4: `m_loadedPackageDirs` 非版本感知（违反 project_memory 约束）

[SynthrtEngine.cpp#L178](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SynthrtEngine.h) 类型为 `std::set<std::filesystem::path>`。project_memory 明确要求：

> `m_loadedPackageDirs` must use a version-aware storage structure to support multiple versions of the same package ID

lite 用 `stablePackagePath()` 规范化路径后插入 set，仅能识别"路径相同即已加载"，无法区分同 packageId 不同 version 的两个并行声库。

### P5: `SingerModelSession` 是不必要的映射层（违反 ARCH-04）

[SingerModelSession.h](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SingerModelSession.h) 把 `SingerIdentifier → ModelSet` 缓存到 map：

```cpp
class SingerModelSession {
    SingerIdentifier m_identifier;
    ds::infer::ModelSet m_modelSet;        // 直接拥有
    std::mutex m_modelSetMutex;             // 串行化 acquire
public:
    Expected<Model> acquire(StageKind);    // load + 返回 inference + importOptions
};
```

问题：
- ARCH-04 要求"直接句柄而非映射层"；`ModelSetHandle` 已是直接句柄
- 互斥锁串行化 `acquire`，但 `ModelSet::load` 本身可重入（V2 lifecycle）
- 缺失 stale 检测：刷新后旧 `SingerModelSession` 仍可 `acquire`，调用方拿到陈旧模型
- `SingerIdentifier` 与 `ds::bank::SingerRef`（packageId+singerId+version）双向转换在 SynthrtEngine 中频繁发生

### P6: `findSinger` 错误码不一致

[SynthrtEngine.cpp#L573](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SynthrtEngine.cpp)：

```cpp
return Error(ErrorCode::PackageVersionConflict, "Singer ID is ambiguous across catalog packages");
```

synthrt 已为该场景提供 `SvsSingerAmbiguous` (604)，与 `G2pVersionAmbiguous` (321) 镜像（D-41/D-42）。lite 用 `PackageVersionConflict` 表达同一含义，错误码不一致导致宿主无法统一处理。

### P7: G2P ONNX driver 注册样板代码散落 lite

[SynthrtEngine.cpp#L73-L184](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SynthrtEngine.cpp) 定义了两个 file-local adapter 类（`G2pOnnxSessionTask` / `G2pOnnxSessionFactory`），共约 110 行通用样板：
- 把 G2P `SessionTask` 适配到推理 `InferenceSession`
- 强制 `useCpu=true` 避免 GPU 争用
- 持有 `shared_ptr<InferenceDriver>` 防止 Runtime ObjectPool 销毁先于 G2P Manager

这是**跨模块边界适配代码**，本应归 synthrt 提供（与 `setupOnnxInferenceDriver` 平级），而非每个宿主重复实现。lite 之外，dsinfer-cli、C ABI、未来 Python 宿主都需重复。

### P8: extractors 与 voicebank 生命周期耦合

`SynthrtEngine::acquirePitchExtractionOperation()` / `acquireMidiExtractionOperation()` 通过 `shared_lock<shared_mutex>` 保护 Runtime 生命周期，避免 shutdown 时 use-after-free。这是 lite 特有的 shutdown-aware 锁模式，与 voicebook/g2p 无直接关系，但当前与 voicebank refresh 共用 `m_runtimeLifecycleMutex`。

迁移到 VoicebankSession 后，extractors 路径不变（仍直接使用 Runtime + `RuntimeOperationLease`），但应与 VoicebankSession 的 shutdown 解耦，避免互相阻塞。

---

## 3. 与 synthrt 现有 API 的对应关系

下表用于验证 synthrt 已提供的 API 是否覆盖 lite 所有需求（详细 API 引用见 [04-interface-contract.md](file:///d:/projects/synthrt/docs/lite-integration/04-interface-contract.md)）：

| lite 当前能力 | synthrt 现有 API | 是否覆盖 |
|---|---|---|
| 扫描声库 + 生成快照 | `VoicebankSession::refresh()` / `refreshAsync()` / `snapshot()` | ✅ |
| 包级状态 + manifest | `VoicebankSnapshot::packages` + `manifests` (TD-01) | ✅ |
| singer 三态 + 诊断 | `VoicebankSession::capabilitySummary(SingerRef)` | ✅ |
| 包加载/卸载 | `loadVoicebank(packageId, version)` / `unloadVoicebank` / `loadedVoicebanks()` | ✅ |
| 创建 ModelSet 句柄 | `createModelSet(SingerRef)` / `ensureModelSet(SingerRef)` | ✅ |
| ModelSet 生命周期 | `ModelSetHandle::load/start/stop/unload/unloadAll/isLoaded` | ✅ |
| Stale 检测 + 重试 | `ModelSetHandle::isStale()` + `start()` 返回 `StaleModelSet` | ✅ |
| G2P 转换 | `convertG2p(SingerRef, lang, inputs)` (version-aware 路由) | ✅ |
| S2P 转换 | `convertS2p(SingerRef, lang, pronunciation)` | ✅ |
| 音素校验 | `validatePhonemes(SingerRef, phonemes)` | ✅ |
| G2P/S2P 模块按需加载 | `ensureLanguageReady(packageId, version, lang)` | ✅ |
| ONNX 推理驱动 setup | `srt::driver::setupOnnxInferenceDriver(runtime, pluginRoot, config)` | ✅ |
| **G2P ONNX driver setup** | **缺失**（lite 自实现 110 行 adapter） | ❌ → A1 补齐 |

**结论**：除 P7（G2P ONNX driver setup helper）外，synthrt 现有 API 完全覆盖 lite 需求。本轮 synthrt 侧改动仅为追加 A1 一个 helper + 文档更新。

---

## 4. 风险评估

| 风险项 | 影响 | 缓解 |
|---|---|---|
| lite 推理主链路回归（InferAcousticTask 等 7 个 task） | 高 | 分阶段迁移：先迁移 SynthrtEngine 内部，保留旧 `acquireSingerSession` API 作为转发；task 侧逐个迁移并跑回归 |
| `Manager::initialize()` 冻结 G2P context | 中 | 迁移后 `refresh()` 在 `initializeModels()` 之后调用 `updateMetadata()` 会返回错误；由 `RefreshResult.diagnostics` 上报，不静默 |
| `RuntimeOperationLease` 与 VoicebankSession shutdown 互锁 | 中 | extractors 路径不经过 VoicebankSession；shutdown 序列保留先 `unloadSinger` 后释放 Runtime |
| lite UI 层依赖 `SingerIdentifier`（Qt 类型）而 synthrt 用 `SingerRef`（std 类型） | 低 | `SingerIdentifier` ↔ `SingerRef` 转换函数保留在 lite adapter 中，集中一处 |
