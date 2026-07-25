# lite ↔ synthrt 简洁可靠接口契约

日期: 2026-07-26（v7 修订：auto-init 触发条件勘误（`if (svc)` 而非 `g2pPluginPaths` 非空）；v6 修订：错误码数值勘误 + Phase A/B 完成状态同步）
状态: ☑ 接口已落地（Phase A 完成 commits 37e5b3d/d0015af/4e0a3ff；Phase B 完成 commits dcb2b61a/c323aee2/f652052f/d8e8626f；C1/C2 待回归）
依据: [docs/design/design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) ARCH-03/04/05, ROBUST-01/05

---

## 1. 契约总览

迁移后 lite 与 synthrt 的接口边界**仅 4 个直接接触点**：

```
ds-editor-lite
   │
   ① SynthrtEngine 持有 SessionResources 资源 ──→ srt::core::Runtime
   │                                              srt::g2p::LanguageService (shared_ptr)
   ② SynthrtEngine.session() ─────────────────→ ds::session::VoicebankSession
   ③ SynthrtEngine.runtime() ─────────────────→ srt::core::Runtime (extractors 用)
   ④ setup helpers ──────────────────────────→ srt::driver::setupOnnxInferenceDriver
                                                srt::g2p::setupG2pOnnxDriver
```

**已消除**的间接接触点（v6 状态：B1c 已彻底删除而非桥接，commit f652052f）：
- `PackageCatalog` ↔ `VoicebankScanner`（已删除文件，合并到 VoicebankSession 内部）
- `SingerModelSession` ↔ `ModelSet`（已删除文件，合并到 `ModelSetHandle`）
- `m_loadedPackageDirs` ↔ `Runtime::loadPackage`（已删除成员，合并到 `VoicebankSession::loadVoicebank`）
- lite 自实现 `G2pOnnxSessionTask`/`G2pOnnxSessionFactory` adapter（已删除，合并到 A1 `setupG2pOnnxDriver`）
- `SynthrtEngine::refreshVoicebanks`/`singerSnapshot`/`findSinger`/`packageDirectory`/`acquireSingerSession`/`resolveLanguageRoute`/`resolveS2pResource`（已删除，调用方直接用 `VoicebankSession` API）

> **v6 核对**：lite 现有代码**不存在** `SynthrtEngine::toSingerRef`/`toSingerIdentifier` 显式 helper（v2 误报）。`SingerIdentifier::operator SingerRef()` 隐式转换为**新增**（B1a 已落地，commit dcb2b61a），非替换。

---

## 2. API 调用映射表

### 2.1 初始化序列（SynthrtEngine::initialize）

| 步骤 | 旧 API（v2 组件式） | 新 API（v3 会话式） |
|---|---|---|
| ONNX 推理 driver | `srt::driver::setupOnnxInferenceDriver(runtime, pluginRoot, cfg)` | **不变** |
| G2P ONNX driver | lite 自实现 adapter（110 行） | `srt::g2p::setupG2pOnnxDriver(runtime, g2pPluginPaths)` |
| Extractor plugins | `plugins->addPluginPath(kPitchExtractorPluginIid, ...)` | **不变** |
| Voicebank 扫描 | `m_catalog.prepareRefresh(paths)` + `commit(candidate)` | `m_session.setRoots(paths)` + `m_session.refresh()` → `RefreshResult`（v3 修正：refresh 无参数） |
| LanguageService 初始化 | `m_langSvc.initialize(pluginPaths, g2pPkgs, pkgDirsMap)` (deprecated) | **自动初始化**：`SessionResources.languageService` 非空时 `refresh()` 内部调用 `initializeMetadata/updateMetadata`，传入 `g2pPluginPaths` + `officialG2pPackages` 作为参数（[VoicebankSession.cpp:583-620](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp)，触发条件 `if (svc)`）；lite 无需手动调用 |

### 2.2 包/singer 查询

| 旧 API | 新 API |
|---|---|
| `refreshVoicebanks(paths)` → `shared_ptr<PackageCatalog::Snapshot>` | `m_session.setRoots(paths)` + `m_session.refresh()` → `RefreshResult`，使用 `result.snapshot`（v3 修正） |
| `m_catalog.snapshot()` | `m_session.snapshot()` |
| `singerSnapshot(identifier)` → `SingerSnapshot`（**无外部调用方**） | `m_session.snapshot()->findSinger(SingerRef)`（A2） |
| `findSinger(singerId)` → `SingerIdentifier`（**无外部调用方**；UI 用 `PackageManager::findSingerByIdentifier`） | `PackageManager::findSingerBySingerId(singerId)` 内部调 `snapshot->findSingersBySingerId`（A2），歧义返回 `SvsSingerAmbiguous` |
| `packageDirectory(identifier)` → `path`（**无外部调用方**） | `ensureModelSet` 内部自动 `loadVoicebank`；查询用 `snapshot->findPackage(packageId, version)`（A2） |
| 三态 availability | `m_session.capabilitySummary(SingerRef)` → `SingerCapabilitySummary{availability, languages, phonemes, mixableSpeakers, diagnostics}` |

### 2.3 包加载/卸载（新增 lite 可用能力）

| 旧 API | 新 API |
|---|---|
| `m_runtime.loadPackage(pkgDir)` + `m_loadedPackageDirs.insert(stablePath)` | `m_session.loadVoicebank(packageId, version)` → `LoadResult{NewlyLoaded, AlreadyLoaded}` |
| `unloadSinger()`（清空 sessions map，不卸载包） | `m_session.unloadVoicebank(packageId, version, ForceUnloadTag{})` |
| `m_loadedVoicebanks` 查询（缺） | `m_session.loadedVoicebanks()` → `vector<LoadedVoicebankInfo>` |

### 2.4 ModelSet 句柄

| 旧 API | 新 API |
|---|---|
| `acquireSingerSession(identifier)` → `shared_ptr<SingerModelSession>` | `m_session.ensureModelSet(SingerRef)` → `shared_ptr<ModelSetHandle>` |
| `session->acquire(kind)` → `Model{inference, importOptions}` | `handle->load(kind)` → `NO<Inference>` + `handle->stages().find(kind)->options` |
| `inference->start(input)` | `handle->start(kind, input)` → `NO<TaskResult>` |
| `inference->stop()` | `handle->stop(kind)` |
| `m_modelSet.unload(kind)` / `unloadAll()` | `handle->unload(kind)` / `handle->unloadAll()` |
| `m_modelSet.isLoaded(kind)` | `handle->isLoaded(kind)` |
| stale 检测（缺） | `handle->isStale()` 或 `start()` 返回 `StaleModelSet` |
| 重试（缺） | 一次重试：丢弃旧 handle → `ensureModelSet(ref)` → `start()` |

### 2.5 G2P / S2P / 音素校验

| 旧 API | 新 API |
|---|---|
| `resolveLanguageRoute(identifier, lang)` → `LanguageRoute`（3-arg deprecated） | `m_session.convertG2p(SingerRef, lang, inputs)` 直接转换（路由内部完成） |
| `resolveS2pResource(identifier, lang)` → `shared_ptr<LanguageResource>` | `m_session.convertS2p(SingerRef, lang, pronunciation)` 直接转换 |
| G2P/S2P 模块按需加载（缺） | `m_session.ensureLanguageReady(packageId, version, lang)` |
| `m_langSvc.convert(...)`（4-arg deprecated） | `m_session.convertG2p(SingerRef, lang, inputs)`（内部 version-aware 路由） |
| 音素校验（缺） | `m_session.validatePhonemes(SingerRef, phonemes)` → `Expected<void>` |

### 2.6 类型转换

| lite 类型 | synthrt 类型 | 转换方式 |
|---|---|---|
| `SingerIdentifier` (QString + QVersionNumber) | `ds::bank::SingerRef` (std::string + std::string version) | **隐式转换** `operator SingerRef() const`（B1a.1，新增）— 调用方无需显式 helper |
| `QString` (lyric/lang) | `std::string` | `qstr.toStdString()` / `qstr.toUtf8()`（边界就地转换） |
| `QVersionNumber` | `stdc::VersionNumber` | `stdc::VersionNumber::fromString(qver.toString().toStdString())`（仅 PackageManager 反查时使用） |
| `SingerRef` → `SingerIdentifier`（UI 反向显示） | 边界就地转换：`QString::fromStdString(ref.singerId)` 等 | 无集中 helper |

**核心原则**：`SingerIdentifier` 提供隐式 `operator SingerRef()`（v3 新增）让调用方写 `session.ensureModelSet(identifier)` 一行可读代码，编译器自动转换。UI 反向显示场景就地用 `QString::fromStdString(...)`，不引入新 helper。v3 核对：lite 不存在 `toSingerRef`/`toSingerIdentifier` 显式 helper（v2 误报）。

---

## 3. 错误码契约

迁移后 lite 期望接收并处理的 synthrt 错误码（[project_memory](file:///c:/Users/99662/.trae-cn/memory/projects/-d-projects-synthrt/project_memory.md) ErrorCode 分层；数值源 [include/synthrt/Core/Support/Diagnostic.h](file:///d:/projects/synthrt/include/synthrt/Core/Support/Diagnostic.h)）：

| ErrorCode | 数值 | lite 处理策略 |
|---|---|---|
| `InferenceNotInitialized` | 200 | Runtime 未初始化；fatal，提示重启 |
| `InferenceOutputEmpty` | 212 | ONNX 输出空，提示模型或输入异常 |
| `InferenceDataTypeMismatch` | 213 | ONNX dataType 不匹配，提示模型版本不兼容 |
| `StaleModelSet` | 216 | task 侧一次重试：丢弃旧 handle → `ensureModelSet` → `start` |
| `LoadFailed` | 217 | 标记 singer Disabled，UI 提示模型损坏 |
| `RuntimePackageNotLoaded` | 218 | 调用 `loadVoicebank(packageId, version)` 后重试 |
| `G2pVersionAmbiguous` | 321 | UI 提示用户选择版本（不应触发，因 `SingerRef.version` 非空） |
| `SvsSingerNotFound` | 600 | UI 提示声库缺失 |
| `SvsSingerAmbiguous` | 604 | UI 提示用户选择版本（多版本 singerId 场景） |

> **v6 数值勘误**：v3 文档 `InferenceNotInitialized=211` / `InferenceOutputEmpty=213` / `InferenceDataTypeMismatch=214` / `SvsSingerNotFound=605` 4 处数值错误；v6 已对照 [Diagnostic.h#L66-L137](file:///d:/projects/synthrt/include/synthrt/Core/Support/Diagnostic.h#L66-L137) 实测修正为 `200 / 212 / 213 / 600`。同时 lite 代码 [PackageManager.cpp:352](file:///d:/projects/ds-editor-lite/src/app/Modules/PackageManager/PackageManager.cpp#L352) 注释中 `SvsSingerNotFound (605)` 已同步勘误为 `600`（commit 0a88862d）。

**禁止**：lite 用 `error.message()` 子串匹配判断错误类型（违反 ARCH-02）。统一用 `error.code()` 与上述枚举值比较。

**错误显示**：lite 关键路径用 `error.toString()` 显示（含 `[Code] message\n  at file:line:function`），次要路径用 `error.messageWithLocation()`（project_memory: "lite关键路径 must use `toString()` for errors, 次要路径 use `messageWithLocation()`"）。

---

## 4. 线程安全契约

| 调用上下文 | 线程 | 线程安全保证 |
|---|---|---|
| `SynthrtEngine::initialize()` | 主线程（启动） | 持有 `m_runtimeLifecycleMutex` 独占锁 |
| `m_session.refresh()` | Lite worker 线程 | VoicebankSession 内部 mutex；并发 refresh 共享一次扫描 |
| `m_session.refreshAsync()` | （lite 不使用，CLI/C ABI 用） | `shared_future<RefreshResult>` |
| `subscribeRefresh(callback)` | Lite 主线程订阅 | callback 在 VoicebankSession 内部线程触发；lite 用 `QMetaObject::invokeMethod` 切回主线程 |
| `convertG2p` / `convertS2p` / `validatePhonemes` | Lite worker（推理 task） | const 方法，多线程读 snapshot 安全 |
| `ensureModelSet` / `loadVoicebank` | Lite worker | VoicebankSession 内部 mutex 串行化包加载 |
| `ModelSetHandle::load/start/stop/unload` | Lite worker（持有 handle 的 task） | 实例非重入；同一 handle 同一 stage 必须串行 |
| Extractors（`acquirePitchExtractionOperation` 等） | Lite worker | `shared_lock<shared_mutex>` 与 shutdown 序列协调 |

**禁止**：在 `subscribeRefresh` callback 中直接操作 UI 模型；必须切回主线程。

---

## 5. 生命周期契约

### 5.1 SynthrtEngine 成员析构顺序（声明顺序保证）

```cpp
class SynthrtEngine {
    srt::core::Runtime m_runtime;                          // 1. 最后析构
    std::shared_ptr<srt::g2p::LanguageService> m_langSvc;  // 2.
    ds::session::VoicebankSession m_session;               // 3. 先析构（释放 ModelSetHandle、unloadVoicebank）
};
```

VoicebankSession 析构时：
- 等待所有 in-flight refresh 完成或取消
- 标记所有 ModelSetHandle 为 stale（让运行中 task 自然结束）
- 不主动 unload 已加载包（保留给 Runtime 析构时清理）

### 5.2 shutdown 序列

```cpp
void SynthrtEngine::shutdown() noexcept {
    m_aboutToQuit.store(true);
    // m_session 析构由成员析构完成，不需显式调用
    // extractors 通过 RuntimeOperationLease 检查 m_aboutToQuit 退出
}
```

### 5.3 ModelSetHandle 生命周期

- 由 `ensureModelSet(ref)` / `createModelSet(ref)` 创建，返回 `shared_ptr<ModelSetHandle>`
- lite task 持有 `shared_ptr`，task 结束自动释放
- session refresh 后旧 handle `isStale()==true`，但已 `start()` 的任务可继续使用旧模型完成
- `handle->unload(kind)` 后 `isLoaded(kind)==false`，下次 `start` 前需重新 `load`

---

## 6. 不变量（synthrt 侧保证）

lite 可信赖以下不变量，由 synthrt 在 [docs/modules/ds-session.md](file:///d:/projects/synthrt/docs/modules/ds-session.md) 中承诺：

1. **快照不可变**：`VoicebankSession::snapshot()` 返回 `shared_ptr<const VoicebankSnapshot>`，引用的 snapshot 永不修改
2. **generation 单调**：每次成功 refresh 后 `snapshot->generation` 严格递增
3. **loadVoicebank 幂等**：同 `(packageId, version)` 二次加载返回 `AlreadyLoaded`，不重复加载
4. **ModelSetHandle 绑定 generation**：handle 创建时的 snapshot generation 永不改变；`isStale()` 与当前 session generation 比较
5. **StaleModelSet 仅来自 `start()`**：`load`/`stop`/`unload`/`unloadAll`/`isLoaded` 在 stale handle 上仍允许
6. **convertG2p/convertS2p 在 metadataReady 后可用**：路由解析仅需 manifest 元数据，不需 ONNX 模型；`convert`（含 ONNX 推理）需 `modelsReady()==true`
7. **错误码值稳定**：ARCH-02 同 Level 内错误码仅追加不重排
8. **路径序列化**：所有跨边界路径用 `stdc::path::to_utf8()`，lite 端用 `StringUtils::qstr_to_path`/`toUtf8` 适配
9. **snapshot 查询方法语义稳定**（A2 新增）：`findSinger(SingerRef)` / `findSingersBySingerId(singerId)` / `findPackage(packageId, version)` / `findManifest(packageId, version)` 返回 raw pointer，调用期间 snapshot 不可变；version 比较通过 `stdc::VersionNumber::operator==` 处理 "1.0" == "1.0.0" 规范化
10. **LanguageService 自动初始化**（v3 确认 / v7 勘误触发条件）：`SessionResources.languageService` 非空时（实际触发条件 `if (svc)`，非 `g2pPluginPaths` 非空），`refresh()` 内部按 `metadataReady()` 状态调用 `initializeMetadata()`（首次）或 `updateMetadata()`（增量），将 `g2pPluginPaths` + `officialG2pPackages` 作为参数传入；失败放入 `RefreshResult.diagnostics`（Warning 级）不静默；`convertG2p` 在 `modelsReady()==false` 时返回 `G2pNotInitialized` 显式错误
11. **refresh() 无参数**（v3 确认）：路径通过 `setRoots()` 设置；`refresh()` 使用已设置的 roots 执行扫描；`setRoots()` 立即更新 `roots()` 返回值但已发布 snapshot 保留旧值直到下次 refresh（D-35）

---

## 7. 接口稳定性承诺

| 接口层 | 稳定性 | 变更策略 |
|---|---|---|
| `ds::session::VoicebankSession` 公共 API | Level=2 冻结 | 仅追加新方法，不修改现有签名；破坏性变更提升 Level=3 |
| `ModelSetHandle` 公共 API | Level=2 冻结 | 同上 |
| `ErrorCode` 枚举值 | 数值稳定 | 仅追加新值，不重排现有值 |
| `srt::g2p::setupG2pOnnxDriver` (A1) | Level=2 已落地（commit 37e5b3d） | 同 Level=2 冻结规则 |
| `VoicebankSnapshot` 字段 | 字段冻结 | 仅追加新字段，不删除/重命名现有字段 |
| `VoicebankSnapshot::findSinger/findPackage/findManifest` (A2) | Level=2 已落地（commit d0015af） | const 方法，仅追加新查询方法，不修改现有字段 |
| `RefreshResult` 字段 | 字段冻结 | 同上 |
| `SingerCapabilitySummary` 字段 | 字段冻结 | 同上 |

**lite 侧**：`SynthrtEngine` 与 `SingerIdentifier` 均为 lite 内部代码，可自由重构；但 `SingerIdentifier` 已 `Q_DECLARE_METATYPE` 且被 Qt 元类型系统使用，字段名/类型不可破坏性变更（如修改字段类型需同步更新 `qRegisterMetaType`）。新增 `operator SingerRef()` 隐式转换不破坏现有 ABI（B1a 已落地，commit dcb2b61a）。
