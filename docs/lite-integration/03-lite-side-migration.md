# lite 侧分阶段迁移方案（v7 反映 B1c/B3 已完成 + auto-init 触发条件勘误；v6 错误码勘误）

日期: 2026-07-26（v7 修订：auto-init 触发条件勘误（`if (svc)` 而非 `g2pPluginPaths` 非空）；v6 修订：错误码勘误 + lite 注释同步修复；v5 反映 B1c/B3 完成，全部迁移结束）
状态: ☑ 全部完成（B1a=dcb2b61a, B1b=c323aee2, B1c=f652052f, B3=d8e8626f；lite 注释勘误 0a88862d）
依赖: [02-synthrt-side-changes.md](file:///d:/projects/synthrt/docs/lite-integration/02-synthrt-side-changes.md) Phase A 完成

---

## 总览

将 `d:\projects\ds-editor-lite\src\app\Modules\SynthrtEngine\` 从 v2 组件式 facade **直接迁移**到 v3 会话式入口。**不留转发桥接**：旧公共 API 一次性删除，编译错误驱动 **10 个文件**迁移到 `VoicebankSession` API。

> **v3 核对修正（vs v2）**：
> 1. `VoicebankSession::refresh()` **无参数** — 路径通过 `setRoots()` 设置（v2 的 `m_session.refresh(searchPaths)` 错误）
> 2. `SessionResources.languageService` 非空时 `refresh()` 自动初始化 LanguageService（实际触发条件为 `if (svc)` 即 `languageService != nullptr`；传入 `g2pPluginPaths` 作为参数；v2 独立 B2 阶段不必要，已合并到 B1）
> 3. 实际调用方 **10 个文件**（非 23）：InferEngine 单点 chokepoint + 5 个直接调用方 + 4 个 task 经 InferEngine 间接调用
> 4. `SynthrtEngine::findSinger`/`singerSnapshot`/`packageDirectory` **无外部调用方**（仅 SynthrtEngine.cpp 内部）；UI 的 `findSinger` 是 `PackageManager::findSingerByIdentifier`（保留模块）
> 5. lite 不存在 `toSingerRef`/`toSingerIdentifier` helper（v2 误报，已移除"删除"措辞）
> 6. `m_langSvc` 现为值类型，需改为 `shared_ptr` 以满足 `SessionResources`

> **v4 实际状态修订（vs v3）**：
> 1. **B1a/B1b 已完成**（commits dcb2b61a/c323aee2, 2026-07-25）：双 API 共存阶段结束，调用方已迁移到 session API
> 2. **`languageService()` 返回类型修正**：v3 计划改为 `shared_ptr`，实际实现保留旧签名 `const LanguageService &`（不破坏 LyricDialog/GetPronunciationTask 旧调用方），**新增** `languageServicePtr()` 返回 `shared_ptr` 满足 `SessionResources`
> 3. **B1b-2 缓存层部分迁移**：PackageManager 已用 `session().setRoots()+refresh()`，但缓存层未完全基于 `snapshot->generation`（留给 B3 一并处理）

> **v5 实际状态修订（vs v4）**：
> 1. **B1c 已完成**（commit f652052f, 2026-07-25）：旧 API、旧成员、中间层文件、G2P ONNX adapter 类全部删除；`SynthrtEngine::acquireSingerSession` 也被删除（与 v4 预期不同，详见 B1c 章节）
> 2. **B3 已完成**（commit d8e8626f, 2026-07-25）：`PackageManager::findSingerBySingerId` 已实现；缓存层 staleness 检查已添加；`SvsSingerAmbiguous`/`SvsSingerNotFound` 已在 lite 代码中使用
> 3. **B1b-3 文档错误修正**：v4 标称 `GetPhonemeNameTask` 已迁移到 `session().ensureLanguageReady + session().convertS2p`，实际仍调用旧 `SynthrtEngine::resolveS2pResource(...)`；B1c 补完成（详见 B1c 章节 §6）
> 4. **全部迁移结束**：B1a/B1b/B1c/B3 全部完成；B2 合并到 B1a/B1b（无独立 commit）

> **v6 错误码勘误（vs v5）**：
> 1. **B1a-T02 文字错误修正**：原 `session.snapshot()->generation` 缺少调用括号；v6 修正为 `session().snapshot()->generation`
> 2. **B3.1 代码示例勘误**：v5 标称"实际实现"的代码块与 lite `PackageManager.cpp` 真实实现不符（简化了 `fmt::format` 消息、用位置初始化器、变量名 `match` 而非 `s`）；v6 修正为与真实代码一致（详见 B3.1）
> 3. **B3 错误码勘误**：`SvsSingerNotFound` 实际值为 `600`（[Diagnostic.h#L130](file:///d:/projects/synthrt/include/synthrt/Core/Support/Diagnostic.h#L130)），v5 文档中部分位置误标 `605`；v6 同步修正。lite 代码 [PackageManager.cpp:352](file:///d:/projects/ds-editor-lite/src/app/Modules/PackageManager/PackageManager.cpp#L352) 注释中 `(605)` 也已同步勘误为 `(600)`（commit 0a88862d）

### 目标态结构

```
SynthrtEngine (singleton, QObject)
  ├── srt::core::Runtime m_runtime                       [借给 VoicebankSession 与 extractors]
  ├── std::shared_ptr<srt::g2p::LanguageService> m_langSvc  [shared_ptr, 借给 VoicebankSession]
  ├── ds::session::VoicebankSession m_session            [唯一 voicebank/G2P/inference 入口]
  │     └── SessionResources{ &m_runtime, m_langSvc, g2pPluginPaths, officialG2pPackages }
  ├── RuntimeOperationLease (shared_lock 模式)            [extractors 用，不经过 session]
  └── (无 SingerIdentifier ↔ SingerRef helper；转换由隐式 operator 完成)
```

### 删除清单（v5 已全部完成）

**完全删除的文件**（B1c 已执行，commit f652052f）：
- `src/app/Modules/SynthrtEngine/PackageCatalog.h` / `.cpp` — ☑ 已删除
- `src/app/Modules/SynthrtEngine/SingerModelSession.h` / `.cpp` — ☑ 已删除

**完全删除的 SynthrtEngine 成员**（B1c 已执行）：
- `PackageCatalog m_catalog` — ☑ 已删除
- `std::set<std::filesystem::path> m_loadedPackageDirs` — ☑ 已删除
- `std::unordered_map<SingerIdentifier, std::shared_ptr<SingerModelSession>> m_singerSessions` — ☑ 已删除
- `std::mutex m_catalogRefreshMutex` — ☑ 已删除
- `QReadWriteLock m_singerRwLock` — ☑ 已删除
- `G2pOnnxSessionTask` / `G2pOnnxSessionFactory` adapter 类（A1 替代） — ☑ 已删除（位于 SynthrtEngine.cpp 匿名 namespace）

**完全删除的 SynthrtEngine 公共 API**（B1c 已执行；以下 API 中部分无外部调用方，删除仅清理内部使用）：
- `refreshVoicebanks(searchPaths, allowReuse)` — ☑ 已删除；调用方应直接用 `session().setRoots(paths)` + `session().refresh()`
- `singerSnapshot(identifier)` — ☑ 已删除；**无外部调用方**（仅 SynthrtEngine.cpp 内部）；内部改用 `session().snapshot()->findSinger(...)`
- `findSinger(singerId)` — ☑ 已删除；**无外部调用方**；UI 层 `findSinger` 均为 `PackageManager::findSingerByIdentifier`（保留模块，B3 改用 `snapshot->findSingersBySingerId`）
- `packageDirectory(identifier)` — ☑ 已删除；**无外部调用方**（仅 `acquireSingerSession` 内部使用）；`ensureModelSet` 内部已自动 `loadVoicebank`
- `acquireSingerSession(identifier)` — ☑ 已删除（v4 预期保留，实际 B1c 删除；详见 B1c §1）
- `resolveLanguageRoute(identifier, lang)` — ☑ 已删除；调用方应直接用 `session().convertG2p(identifier, lang, ...)`
- `resolveS2pResource(identifier, lang)` — ☑ 已删除；调用方应直接用 `session().ensureLanguageReady(...)` + `session().convertS2p(...)`
- `unloadSinger()` — ☑ 已删除
- `singerLoaded` 信号 — ☑ 已删除（仅旧 acquireSingerSession 使用，全项目无 `connect(.*singerLoaded.*)` 连接）

### 保留的 SynthrtEngine 公共 API（v5 最终状态）

- `instance()` / `pluginRoot()` / `initialize(packagePaths, g2pPackagePaths, ep, index)` / `runtimeInitialized()` / `isAboutToQuit()` / `shutdown()`
- `runtime()` — extractors 直接使用
- `languageService()` — **保留旧签名** `const LanguageService &`（不破坏 LyricDialog/GetPronunciationTask 旧调用方）
- `languageServicePtr()` — **B1a 新增**，返回 `std::shared_ptr<srt::g2p::LanguageService>`（满足 `SessionResources`）
- `session()` — **B1a 新增**，暴露 `VoicebankSession &`
- `runtimeOperation()` / `acquirePitchExtractionOperation()` / `acquireMidiExtractionOperation()` — extractors

> **注**：`acquireSingerSession` 在 v4 计划中保留，实际 B1c 已删除（详见 B1c §1）。`InferEngine::acquireSingerSession` 是 **InferEngine 自有方法**（B1b 迁移为新 API，直接调用 `session().ensureModelSet()`），与 `SynthrtEngine::acquireSingerSession` 不同。

---

## B1a: SingerIdentifier 隐式转换 + SynthrtEngine 添加 session（双 API 共存）

**状态：☑ 已完成（commit dcb2b61a, 2026-07-25）**

### 关键技术决策（v4 摘要）

1. **SingerIdentifier 隐式转换**：在 `src/app/Model/AppModel/SingerIdentifier.h` 添加 `operator ds::bank::SingerRef() const`（非 `explicit`），字段映射 `packageId/singerId/packageVersion.toString()` → `std::string`；保留 QString 字段（UI 层 `Q_DECLARE_METATYPE` + 信号槽需要）；不引入 `toSingerRef`/`toSingerIdentifier` helper。

2. **m_langSvc 改 shared_ptr**：从值类型改为 `std::shared_ptr<srt::g2p::LanguageService>`，满足 `SessionResources.languageService` 字段类型要求。

3. **languageService() 返回类型修正（vs v3 计划）**：v3 计划改为 `shared_ptr`，实际实现保留旧签名 `const LanguageService &`（避免破坏 LyricDialog/GetPronunciationTask 旧调用方），**新增** `languageServicePtr()` 返回 `shared_ptr`。这是相对 v3 计划的微调，更保守。

4. **m_session 添加 + 双 API 共存**：`SynthrtEngine` 新增 `ds::session::VoicebankSession m_session` 成员与 `session()` API；旧 API（refreshVoicebanks/acquireSingerSession/singerSnapshot 等）**全部保留**，调用方在 B1b 迁移，B1c 才删除。

5. **SessionResources 自动初始化**：`initialize()` 中重新构造 `m_session`，填充 `SessionResources{ &m_runtime, m_langSvc, g2pPluginPaths, officialG2pPackages }`；`refresh()` 内部当 `languageService` 非空时（触发条件为 `if (svc)`，非 `g2pPluginPaths` 非空）自动调用 `LanguageService::initializeMetadata/updateMetadata`，传入 `g2pPluginPaths` + `officialG2pPackages`（[VoicebankSession.cpp#L583-L620](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp)），**B2 阶段不必要**（已合并到 B1a）。

6. **initializeG2pOnnxDriver 改用 A1**：从自实现 `G2pOnnxSessionTask`/`G2pOnnxSessionFactory` 改为调用 `srt::g2p::setupG2pOnnxDriver(m_runtime, g2pPluginPaths)`。**注意**：旧 adapter 类仍存在（B1c 删除）。

7. **refresh() 无参数**：路径通过 `setRoots()` 设置（v3 核对修正）；`initialize()` 中先 `setRoots(searchPaths)` 再 `refresh()`。

8. **shutdown 序列**：`m_session` 析构先于 `m_runtime`（成员声明顺序保证）；无 use-after-free。

### B1a 验证

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B1a-T01 | `SingerRef.version` 规范化与 `QVersionNumber::toString()` 一致 | `"1.0" == "1.0.0"`，无尾随零差异 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1a-T02 | `SynthrtEngine::initialize()` 流程 | `session().snapshot()->generation >= 1`；`languageServicePtr()->metadataReady()==true`（自动初始化） | ☑ 已验证（C1/C2 阶段统一回归） |
| B1a-T03 | 双 API 共存编译 | 旧 API 与新 `session()` 同时可用；调用方未迁移也能编译 | ☑ 已验证（commit dcb2b61a） |
| B1a-T04 | `SynthrtEngine::shutdown()` 序列 | `m_session` 析构先于 `m_runtime`（成员声明顺序）；无 use-after-free | ☑ 已验证（C1/C2 阶段统一回归） |

**回归**：
- `domains/ds-bank/unittests/tst_ds_editor_lite_scenarios.cpp` 全过 — **C1/C2 阶段统一回归**
- lite 启动冒烟：打开应用，无 fatal log — **C1/C2 阶段统一回归**

---

## B1b: 逐文件迁移调用方（InferEngine chokepoint + 5 个直接调用方）

**状态：☑ 已完成（commit c323aee2, 2026-07-25）**

### 迁移清单（v4 实际完成范围）

#### B1b-1: InferEngine + ActiveInference 适配（单点 chokepoint，影响 4 个 task）

| 文件 | 旧调用 → 新调用 | 状态 |
|---|---|---|
| `InferEngine.h` | `acquireSingerSession` 返回类型 `shared_ptr<SingerModelSession>` → `shared_ptr<ds::infer::ModelSetHandle>` | ☑ |
| `InferEngine.cpp` | 实现改用 `session().ensureModelSet(identifier)` + `StaleModelSet` 一次重试（D-30） | ☑ |
| `InferTaskCommon.h` ActiveInference | `acquire(shared_ptr<SingerModelSession>, kind)` → `acquire(shared_ptr<ModelSetHandle>, kind)`；内部 `handle->load(kind)` + `handle->model(kind)` + `handle->stages().find(kind)` 构造等价 `Model{inference, importOptions}`；Handle 对外接口不变 | ☑ |
| `InferAcousticTask.cpp` | 类型替换 `SingerModelSession` → `ModelSetHandle`（1-2 行） | ☑ |
| `InferDurationTask.cpp` | 同上 | ☑ |
| `InferPitchTask.cpp` | 同上 | ☑ |
| `InferVarianceTask.cpp` | 同上 | ☑ |

> **v3 核对**：`InferVocoderTask.cpp` **不调用** `acquireSingerSession`（v2 误列）；实际只有 4 个 task（Acoustic/Duration/Pitch/Variance）。B1b 已核对一致。

#### B1b-2: PackageManager（refreshVoicebanks 调用方）

| 文件 | 旧调用 → 新调用 | 状态 |
|---|---|---|
| `PackageManager.cpp` | `SynthrtEngine::instance().refreshVoicebanks(paths, allowReuse)` → `SynthrtEngine::instance().session().setRoots(paths)` + `session().refresh()`；遍历 `result.snapshot->packages` 替代 `catalog->packages`；`snapshot->findSinger(...)` / `findManifest(...)`（A2）替代自实现遍历 | ☑ |

> **B1b-2 遗留已由 B3 完成**：缓存层部分基于 snapshot 但未完全迁移到 `snapshot->generation`（`m_catalogGeneration` 与 `snapshot->generation` 的对齐逻辑未完成），B3 已补完 staleness 检查（详见 B3 章节）。

#### B1b-3: 语言服务调用方（3 个文件）

| 文件 | 旧调用 → 新调用 | 状态 |
|---|---|---|
| `G2pService.cpp` | `resolveLanguageRoute(id, lang)` → `session().convertG2p(id, lang, inputs)` | ☑ |
| `GetPronunciationTask.cpp` | `resolveLanguageRoute(id, lang)` → `session().convertG2p(id, lang, inputs)`；移除 `LanguageRoute` 中间结构 + 手动 `m_langSvc.convert(...)` | ☑ |
| `GetPhonemeNameTask.cpp` | `resolveS2pResource(id, lang)` → `session().ensureLanguageReady(id.packageId, version, lang)` + `session().convertS2p(id, lang, pron)` | ⚠ v4 误报；实际 B1c 补完成（见下） |

> **v5 文档错误修正（B1b-3 GetPhonemeNameTask）**：
> v4 文档标称 B1b-3 已迁移 `GetPhonemeNameTask` 到 `session().ensureLanguageReady + session().convertS2p`。实际核对发现 B1b 子代理误报：B1b 后 `GetPhonemeNameTask.cpp` 仍调用 `SynthrtEngine::instance().resolveS2pResource(...)`（含 `QHash<QString, shared_ptr<LanguageResource>> s2pCache` + try-catch）。**B1c 补完成此迁移**（详见 B1c 章节 §6）。

### 关键技术决策（v4 摘要）

1. **InferEngine chokepoint 策略**：单点修改 `InferEngine::acquireSingerSession` 返回类型 + 实现，4 个 task 仅做类型名替换（`SingerModelSession` → `ModelSetHandle`），调用代码不变。

2. **StaleModelSet 重试**：`ensureModelSet` 返回 `StaleModelSet` 错误时一次重试（D-30）；二次失败则返回空 `shared_ptr`。

3. **ActiveInference::Handle 接口不变**：内部构造 `{inference, importOptions}` 方式改变（从 `handle->load(kind)` + `handle->model(kind)` + `handle->stages().find(kind)`），但对外接口不变，4 个 task 调用代码仅类型替换。

4. **PackageManager Qt 缓存保留**：`m_packageLocator` / `m_singerLocator` Qt 缓存保留（UI 频繁查询需 Qt 类型）；填充逻辑改用 `snapshot->findPackage(...)` / `snapshot->findSinger(...)`。

5. **convertG2p/convertS2p 内部 version-aware 路由**：`VoicebankSession::convertG2p` / `convertS2p` 内部已按 `(packageId, version, language)` 路由（V3-08/V3-10）；lite 仅需迁移调用方，无需额外处理 version 参数（`SingerRef.version` 隐式携带）。

6. **B2 已合并**：v2 独立 B2 阶段（手动 `initializeMetadata` + version-aware 路由）经核对不必要，已合并到 B1a（SessionResources 自动初始化）+ B1b（调用方迁移），无独立 commit。

### B1b 验证

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B1b-T01 | `InferEngine::acquireSingerSession` 返回 `ModelSetHandle` | 4 个 task 编译通过；运行时 `handle->load(kind)` 成功 | ☑ 已验证（commit c323aee2） |
| B1b-T02 | `ActiveInference::acquire` 适配 | `handle.model().inference` 非空；`importOptions` 有效 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1b-T03 | stale 重试 | 模拟 `StaleModelSet` 触发一次重试后成功 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1b-T04 | `PackageManager` 基于 snapshot 查询 | UI 列表显示与旧版一致；staleness 检查由 B3 补完 | ☑ 已验证（C1/C2 阶段统一回归；B3 补 staleness） |
| B1b-T05 | `convertG2p` 直接调用 | G2P 转换结果与旧 `resolveLanguageRoute` + `convert` 一致 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1b-T06 | `convertS2p` + `ensureLanguageReady` | S2P 转换结果与旧 `resolveS2pResource` 一致 | ☑ 已验证（C1/C2 阶段统一回归；B1c 补 GetPhonemeNameTask 迁移） |
| B1b-T07 | extractors 路径不受影响 | `acquirePitchExtractionOperation()` 正常 | ☑ 已验证（C1/C2 阶段统一回归） |

**回归**：
- lite 端到端推理冒烟：Acoustic/Duration/Pitch/Variance 4 个 task 均成功 — **C1/C2 阶段统一回归**
- `domains/ds-bank/unittests/tst_ds_editor_lite_scenarios.cpp` 全过 — **C1/C2 阶段统一回归**

---

## B1c: 删除旧 API + 中间层文件

**状态：☑ 已完成（commit f652052f, 2026-07-25）**

### B1c.0 关键技术决策（v5 新增）

1. **`SynthrtEngine::acquireSingerSession` 也被删除（与 v4 预期不同）**：
   - v4 文档假设 `acquireSingerSession` 签名已在 B1b 迁移为 `shared_ptr<ModelSetHandle>`，B1c 保留
   - 实际核对发现：B1b 只迁移了 `InferEngine::acquireSingerSession`（**InferEngine 自有包装方法**，返回 `shared_ptr<ModelSetHandle>`，直接调用 `session().ensureModelSet()`），`SynthrtEngine::acquireSingerSession` 仍是旧签名（返回 `shared_ptr<SingerModelSession>`），依赖被删除的 `SingerModelSession`/`m_singerSessions`/`m_loadedPackageDirs`/`packageDirectory()`
   - B1c 处置：**删除** `SynthrtEngine::acquireSingerSession`（无外部调用方，`InferEngine::acquireSingerSession` 不调用它，直接调用 `session().ensureModelSet()`）
   - **`InferEngine::acquireSingerSession`（新 API）保留**，是 InferEngine 自有方法

2. **`singerLoaded` 信号删除**：仅旧 `acquireSingerSession` 使用，grep 验证全项目无 `connect(.*singerLoaded.*)` 连接

3. **`initialize()` 清理**：删除 `refreshVoicebanks(vbPaths, true)` + catalog 迭代统计 + `pkgDirs` 构建 + `m_langSvc->initialize(...)` 调用；保留 Runtime/extractors init + G2P plugin paths + G2P ONNX driver setup (A1) + SessionResources 构造 + setRoots + refresh

4. **`shutdown()` 清理**：删除 `unloadSinger()` 调用（`m_session` 由成员析构顺序保证清理）

5. **行为变更：`m_session.refresh()` 失败现在返回 false**：
   - B1a 双 API 共存时 `m_session.refresh()` 失败不返回 false（旧 `m_catalog` 路径仍可用）
   - B1c 删除旧路径后，`m_session.refresh()` 失败现在返回 false（v3 终态行为）

6. **`GetPhonemeNameTask.cpp` 补迁移（B1b-3 文档错误）**：
   - v3/v4 文档标称 B1b-3 已迁移 `GetPhonemeNameTask` 到 `session().ensureLanguageReady + session().convertS2p`
   - 实际代码仍调用 `SynthrtEngine::instance().resolveS2pResource(...)`（B1b 子代理误报）
   - B1c 补完成：移除 `QHash<QString, shared_ptr<LanguageResource>> s2pCache` + try-catch；新增 `QSet<QString> readyLanguages` 缓存 + `VersionUtils::qt_to_stdc(version)` 转换；`resource->convert(pron)` → `session.convertS2p(identifier, lang, pron)`

7. **测试 CMakeLists.txt 清理**：删除对 `SingerModelSession.cpp` 的源文件引用（删除文件的必要 corollary）
   - `src/tests/TestInputConversion/CMakeLists.txt` — 删除 `SingerModelSession.cpp` 引用
   - `src/tests/TestSpeakerMixValidation/CMakeLists.txt` — 同上

8. **`InferEngine::acquireSingerSession` 保留（新 API）**：是 InferEngine 自有方法（非 SynthrtEngine），B1b 已迁移为新 API，直接调用 `session().ensureModelSet()`。与 `SynthrtEngine::acquireSingerSession`（已删除）不同

### B1c.1 删除 SynthrtEngine 旧公共 API（已全部删除）

- `refreshVoicebanks(searchPaths, allowReuse)` — ☑ 已删除
- `singerSnapshot(identifier)` — ☑ 已删除（无外部调用方）
- `findSinger(singerId)` — ☑ 已删除（无外部调用方）
- `packageDirectory(identifier)` — ☑ 已删除（无外部调用方）
- `acquireSingerSession(identifier)` — ☑ 已删除（v4 预期保留，实际 B1c 删除；详见 §1）
- `unloadSinger()` — ☑ 已删除
- `resolveLanguageRoute(identifier, lang)` — ☑ 已删除
- `resolveS2pResource(identifier, lang)` — ☑ 已删除
- `singerLoaded` 信号 — ☑ 已删除（无连接）

### B1c.2 删除中间层文件（已全部删除）

- `src/app/Modules/SynthrtEngine/PackageCatalog.h` / `.cpp` — ☑ 已删除
- `src/app/Modules/SynthrtEngine/SingerModelSession.h` / `.cpp` — ☑ 已删除
- `SynthrtEngine.cpp` 内匿名 namespace 的 `G2pOnnxSessionTask` / `G2pOnnxSessionFactory` adapter 类（A1 替代） — ☑ 已删除

### B1c.3 删除 SynthrtEngine 旧成员（已全部删除）

- `PackageCatalog m_catalog` — ☑ 已删除
- `std::set<std::filesystem::path> m_loadedPackageDirs` — ☑ 已删除
- `std::unordered_map<SingerIdentifier, std::shared_ptr<SingerModelSession>> m_singerSessions` — ☑ 已删除
- `std::mutex m_catalogRefreshMutex` — ☑ 已删除
- `QReadWriteLock m_singerRwLock` — ☑ 已删除

### B1c.4 补迁移 GetPhonemeNameTask（B1b-3 文档错误）

详见 §6。`GetPhonemeNameTask.cpp` 移除 `s2pCache` + try-catch；新增 `readyLanguages` 缓存 + `VersionUtils::qt_to_stdc(version)` 转换；`resource->convert(pron)` → `session.convertS2p(identifier, lang, pron)`。

### B1c.5 测试 CMakeLists.txt 清理

- `src/tests/TestInputConversion/CMakeLists.txt` — ☑ 已删除 `SingerModelSession.cpp` 引用
- `src/tests/TestSpeakerMixValidation/CMakeLists.txt` — ☑ 已删除 `SingerModelSession.cpp` 引用

### B1c 验证

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B1c-T01 | 删除后编译 | 无对旧 API 的残留引用；编译干净 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1c-T02 | `PackageCatalog`/`SingerModelSession` 文件不存在 | 链接成功；无未定义符号 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1c-T03 | 端到端推理 | Acoustic/Duration/Pitch/Variance 4 个 task 全过 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1c-T04 | `G2pOnnxSessionTask`/`G2pOnnxSessionFactory` 已删除 | 无残留引用；A1 `setupG2pOnnxDriver` 仍正常工作 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1c-T05 | `SynthrtEngine::acquireSingerSession` 删除 | `InferEngine::acquireSingerSession` 仍正常；无未定义符号 | ☑ 已验证（C1/C2 阶段统一回归） |
| B1c-T06 | `m_session.refresh()` 失败返回 false | refresh 失败时 `initialize()` 返回 false | ☑ 已验证（C1/C2 阶段统一回归） |
| B1c-T07 | `GetPhonemeNameTask` 已迁移 | S2P 转换结果与旧 `resolveS2pResource` 一致 | ☑ 已验证（C1/C2 阶段统一回归） |

**回归**：
- lite 端到端推理冒烟：Acoustic/Duration/Pitch/Variance 4 个 task 均成功 — **C1/C2 阶段统一回归**
- `domains/ds-bank/unittests/tst_ds_editor_lite_scenarios.cpp` 全过 — **C1/C2 阶段统一回归**

---

## B2: LanguageService 自动初始化（合并自 v2，无需独立迁移）

**状态：☑ 已合并到 B1a/B1b（无独立 commit）**

### v3 核对结论

v2 独立 B2 阶段"手动 `initializeMetadata` + `initializeModels` + version-aware 路由"经核对实际代码后**基本不必要**：

1. **LanguageService 自动初始化**：`SessionResources.languageService` 非空时（实际触发条件为 `if (svc)` 即 `languageService != nullptr`，**非 `g2pPluginPaths` 非空**；`g2pPluginPaths` 仅作为参数传入），`VoicebankSession::refresh()` 内部自动调用 `LanguageService::initializeMetadata()` 或 `updateMetadata()`（[VoicebankSession.cpp#L583-L620](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp)）。B1a 填充 `SessionResources` 后，lite **无需手动初始化** LanguageService。

2. **version-aware 路由**：`VoicebankSession::convertG2p` / `convertS2p` 内部已按 `(packageId, version, language)` 路由（V3-08/V3-10）；lite 仅需在 B1b 第 3 步将调用方迁移到 `session().convertG2p/convertS2p`，无需额外处理 version 参数（`SingerRef.version` 隐式携带）。

3. **`initializeModels` 失败处理**：`refresh()` 将失败放入 `RefreshResult.diagnostics`（Warning 级），不静默；后续 `convertG2p` 因 `modelsReady()==false` 返回 `G2pNotInitialized` 显式错误。

### B2 验证（并入 B1a/B1b 验证项）

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B2-T01 | 单版本启动 | `session().refresh()` 后 `languageServicePtr()->metadataReady()==true` | ☑ 已验证（B1a-T02） |
| B2-T02 | 多版本同 packageId 启动 | 两个 version 的路由独立；`convertG2p` 隔离 | ☑ 已验证（C1/C2 阶段统一回归） |
| B2-T03 | `initializeModels` 失败 | `RefreshResult.diagnostics` 含 Warning；`convertG2p` 返回 `G2pNotInitialized` | ☑ 已验证（C1/C2 阶段统一回归） |
| B2-T04 | 启动后 refresh（新增包） | `RefreshResult.changes.added` 含新包 | ☑ 已验证（C1/C2 阶段统一回归） |
| B2-T05 | 启动后 refresh（移除包） | 旧 `ModelSetHandle` `isStale()==true` | ☑ 已验证（C1/C2 阶段统一回归） |

**回归**：
- `unittests/G2P/tst_update_metadata.cpp` 全过
- `domains/ds-bank/unittests/tst_voicebank_g2p_integration.cpp` 全过

---

## B3: findSinger 多版本歧义改用 SvsSingerAmbiguous

**状态：☑ 已完成（commit d8e8626f, 2026-07-25）**

### v3 核对：`SynthrtEngine::findSinger(singerId)` 无外部调用方

经核对，`SynthrtEngine::findSinger(singerId)` **无外部调用方**（仅 SynthrtEngine.cpp 内部使用，B1c 已删除）。UI 层的 `findSinger` 调用均为 `packageManager->findSingerByIdentifier(...)`（PackageManager 自有方法，基于 `m_singerLocator` Qt 缓存）。

因此 B3 仅需在 PackageManager 中新增一个"按 singerId 反查（可能多版本）"的 helper，供需要按 singerId 查询的场景使用。

### B3.0 关键技术决策（v5 新增）

1. **`findSingerBySingerId` 绕过 Qt locator 缓存**：直接调用 `session().snapshot()->findSingersBySingerId`，始终反映最新 session generation（locator 缓存可能滞后）

2. **复用 `m_catalogGeneration` 字段（不重命名）**：不重命名为 `m_snapshotGeneration`（减小改动面），但语义已变为 snapshot generation

3. **staleness 不主动清空缓存**：const 查询方法不写数据成员（非 mutable），stale 条目滞留内存但**永不被返回**（staleness 检查先于 locator 查找）。满足"不返回 stale 数据"核心不变量

4. **null snapshot 处理**：`isCacheFreshNoLock` 在 snapshot 为 null 时返回 true，使首次查询返回空 locator 而非反复调用 `snapshot()`

5. **`PackageVersionConflict` 残留清理**：PackageManager 目录无残留；B1c 删除的 `PackageCatalog.cpp`/`SynthrtEngine.cpp` 中原有 2 处引用已随 B1c 整体删除

### B3.1 PackageManager::findSingerBySingerId 新增 helper（已完成）

**修改的文件**：
- `d:/projects/ds-editor-lite/src/app/Modules/PackageManager/PackageManager.h` — 新增 `findSingerBySingerId(singerId)` public 声明 + `isCacheFreshNoLock()` private helper 声明 + `<synthrt/Core/Support/Expected.h>` include
- `d:/projects/ds-editor-lite/src/app/Modules/PackageManager/PackageManager.cpp` — 实现 `findSingerBySingerId`（调用 `snapshot->findSingersBySingerId`，多版本返回 `SvsSingerAmbiguous` 604 含版本列表，无匹配返回 `SvsSingerNotFound` 600）+ `isCacheFreshNoLock`（比较 `snapshot->generation` 与 `m_catalogGeneration`）+ 为 `installedPackages`/`findPackageByIdentifier`/`findSingerByIdentifier` 添加 staleness 检查（stale 时返回空驱动 UI 重新触发 refresh）

```cpp
// PackageManager.h 新增
Expected<SingerIdentifier> findSingerBySingerId(const QString &singerId) const;
```

```cpp
// PackageManager.cpp（v6 已与真实代码核对一致）
srt::core::Expected<SingerIdentifier>
    PackageManager::findSingerBySingerId(const QString &singerId) const {
    // B3: reverse-lookup by singerId alone. Queries the VoicebankSnapshot
    // directly (A2) so the answer always reflects the latest session
    // generation, bypassing the Qt locator cache. Multi-version same-singerId
    // scenarios return SvsSingerAmbiguous (604) with a version list the UI
    // can surface for user disambiguation; no match returns
    // SvsSingerNotFound (600). Per ARCH-02 callers categorize errors via
    // error.code(), never by substring matching on error.message().
    const auto snapshot = SynthrtEngine::instance().session().snapshot();
    if (!snapshot) {
        return srt::core::Error(
            srt::core::ErrorCode::SvsSingerNotFound,
            "voicebank snapshot is not available; refresh has not run yet");
    }
    const auto matches = snapshot->findSingersBySingerId(singerId.toStdString());
    if (matches.empty()) {
        return srt::core::Error(
            srt::core::ErrorCode::SvsSingerNotFound,
            "singer not found for singerId: " + singerId.toStdString());
    }
    if (matches.size() > 1) {
        // Multi-version ambiguity (B3-T02). Collect the version strings from
        // each matching SingerSnapshot.ref.version so the UI can present a
        // selection dialog. Versions are already normalized by
        // stdc::VersionNumber::toString() on the synthrt side.
        QStringList versions;
        versions.reserve(static_cast<QStringList::size_type>(matches.size()));
        for (const auto *snap : matches) {
            versions << QString::fromStdString(snap->ref.version);
        }
        const auto msg =
            QStringLiteral("singerId '%1' matches multiple versions: %2; "
                           "specify packageVersion to disambiguate")
                .arg(singerId, versions.join(QStringLiteral(", ")));
        return srt::core::Error(srt::core::ErrorCode::SvsSingerAmbiguous,
                                msg.toStdString());
    }
    const auto &match = *matches.front();
    return SingerIdentifier{
        QString::fromStdString(match.ref.singerId),
        QString::fromStdString(match.ref.packageId),
        QVersionNumber::fromString(QString::fromStdString(match.ref.version)),
    };
}
```

> **v6 修正 vs v5**：v5 文档标称"实际实现"的代码块与真实代码不符（简化了 `QStringLiteral + .arg()` 错误消息为字面量、用 `Error{...}` brace-init 而非 `Error(...)` 构造、变量名简化为 `match`）。v6 已逐行核对 [PackageManager.cpp#L346-L389](file:///d:/projects/ds-editor-lite/src/app/Modules/PackageManager/PackageManager.cpp#L346-L389)，与真实代码一致。同时注释中 `SvsSingerNotFound (605)` 已勘误为 `(600)`（commit 0a88862d）。

### B3.2 缓存层 staleness 检查（已完成）

**`isCacheFreshNoLock` 实现**：

```cpp
bool PackageManager::isCacheFreshNoLock() const {
    const auto snapshot = SynthrtEngine::instance().session().snapshot();
    if (!snapshot) return true;  // null snapshot 时返回 true，使首次查询返回空 locator
    return snapshot->generation == m_catalogGeneration;
}
```

**staleness 检查添加位置**：
- `installedPackages()` — stale 时返回空列表驱动 UI 重新触发 refresh
- `findPackageByIdentifier(...)` — stale 时返回空
- `findSingerByIdentifier(...)` — stale 时返回空

> **设计原则（[design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) ROBUST-01）**：`findSingerBySingerId` 返回 `Expected<SingerIdentifier>`，多版本歧义通过 `SvsSingerAmbiguous`（604）显式传播，无匹配通过 `SvsSingerNotFound`（600）显式传播，符合"Expected 传播错误"原则。

### B3 验证

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B3-T01 | 唯一 singerId 匹配 | 返回 `SingerIdentifier`；`packageVersion` 填充 | ☑ 已验证（C1/C2 阶段统一回归） |
| B3-T02 | 多版本同 singerId | 返回 `Error{SvsSingerAmbiguous}`；code() == 604 | ☑ 已验证（C1/C2 阶段统一回归） |
| B3-T03 | 无匹配 | 返回 `Error{SvsSingerNotFound}`；code() == 600 | ☑ 已验证（C1/C2 阶段统一回归） |
| B3-T04 | lite UI 弹窗解析 `SvsSingerAmbiguous` | 提示用户选择版本；选择后用 `SingerRef.version` 路由 | ☑ 已验证（C1/C2 阶段统一回归） |
| B3-T05 | 缓存层 staleness 检查 | refresh 后 `m_catalogGeneration == snapshot->generation`；stale 时 `installedPackages` 返回空 | ☑ 已验证（C1/C2 阶段统一回归） |

**回归**：
- `domains/ds-bank/unittests/tst_singer_ref.cpp` 全过
- `domains/ds-infer/unittests/catch2/tst_singer_resolver_ambiguity.cpp` 全过

---

## 执行顺序与提交粒度

按 user_rules "完成单个任务后单独提交但不推送"：

```
Commit B1a (dcb2b61a, 2026-07-25): ☑ 已完成
           SingerIdentifier::operator SingerRef()
           + SynthrtEngine 添加 m_session + session()（双 API 共存）
           + m_langSvc 改 shared_ptr + 填充 SessionResources
           + languageServicePtr() 新增（保留旧 languageService() 签名）
           + initializeG2pOnnxDriver 改用 srt::g2p::setupG2pOnnxDriver
Commit B1b (c323aee2, 2026-07-25): ☑ 已完成
           - B1b-1: InferEngine + ActiveInference 适配（4 task 仅类型替换）
           - B1b-2: PackageManager 改用 session().setRoots()+refresh()+snapshot 查询
                    （缓存层 staleness 检查留给 B3）
           - B1b-3: G2pService / GetPronunciationTask → session API
                    GetPhonemeNameTask 误报已迁移，实际 B1c 补完成
Commit B1c (f652052f, 2026-07-25): ☑ 已完成
           删除 SynthrtEngine 旧 API（refreshVoicebanks/singerSnapshot/findSinger/
           packageDirectory/acquireSingerSession/unloadSinger/
           resolveLanguageRoute/resolveS2pResource/singerLoaded 信号）
           + PackageCatalog/SingerModelSession 文件
           + G2pOnnxSessionTask/G2pOnnxSessionFactory adapter 类
           + SynthrtEngine 旧成员（m_catalog/m_singerSessions/m_loadedPackageDirs/
           m_catalogRefreshMutex/m_singerRwLock）
           + initialize()/shutdown() 清理
           + GetPhonemeNameTask 补迁移（B1b-3 文档错误）
           + 测试 CMakeLists.txt 清理（删除 SingerModelSession.cpp 引用）
Commit B2:  ☑ 已合并到 B1a/B1b（无独立 commit）
Commit B3 (d8e8626f, 2026-07-25): ☑ 已完成
           PackageManager::findSingerBySingerId + SvsSingerAmbiguous/SvsSingerNotFound
           + isCacheFreshNoLock staleness 检查
           + installedPackages/findPackageByIdentifier/findSingerByIdentifier staleness 检查
           + PackageVersionConflict 残留随 B1c 整体删除（无独立操作）
```

每 commit 完成后在本文档对应章节末尾追加 `**状态：已完成（commit-hash）**` 标记。

### v6 提交粒度对比 v5/v4

| 维度 | v4 计划 | v5 实际 | v6 文档勘误 |
|---|---|---|---|
| B1 commit 数 | 3 子阶段（B1a/B1b/B1c），B1b 内部 3 步 | B1a/B1b/B1c 各完成（3 commit） | 不变 |
| 总 commit 数 | 已完成 2（B1a/B1b）；待执行 2（B1c/B3） | 已完成 4（B1a/B1b/B1c/B3） | +1 lite 注释勘误（0a88862d） |
| 每步可编译性 | B1a/B1b 各自可编译；B1c/B3 待验证 | 全部可编译（C1/C2 阶段统一回归） | 不变 |
| 回滚粒度 | B1a/B1b 已提交可 revert；B1c/B3 待执行 | 全部已提交可 revert；B1c 是关键节点 | 不变 |
| 文档错误码数值 | — | 4 处错误（`InferenceNotInitialized=211` 等） | v6 全部修正（详见 §B3.1 末尾 v6 修正说明） |

---

## 风险与回滚（v5 修订）

| 风险 | 缓解 |
|---|---|
| B1a/B1b 已提交，回滚风险 | B1a 双 API 共存设计：旧 API 完整保留，revert B1a 仅移除 m_session + session() + languageServicePtr()；B1b 调用方已迁移，revert 需同时回退调用方迁移 |
| B1c 已提交：删除旧 API 后发现遗漏调用方 | B1b 已迁移全部 10 个文件；B1c 编译错误驱动兜底；旧 API 无外部调用方（singerSnapshot/findSinger/packageDirectory）；C1/C2 阶段统一回归已验证 |
| B1c 删除 PackageCatalog/SingerModelSession 后链接错误 | B1b 已不依赖这两个文件；B1c-T02 已验证（C1/C2 阶段统一回归） |
| B1c 删除 G2pOnnxSessionTask/G2pOnnxSessionFactory 后 ONNX 加载失败 | A1 `setupG2pOnnxDriver` 已替代；B1c-T04 已验证 |
| B1c 删除 SynthrtEngine::acquireSingerSession 后链接错误 | InferEngine::acquireSingerSession 不调用它（直接调用 session().ensureModelSet()）；B1c-T05 已验证 |
| B1c GetPhonemeNameTask 补迁移可能引入回归 | B1c-T07 已验证 S2P 转换结果与旧 `resolveS2pResource` 一致 |
| 4 个 DiffSinger task 经 InferEngine 间接迁移可能引入并发缺陷 | ActiveInference 内部 mutex 保留；每个 task 端到端推理冒烟 |
| `Manager` 进程单例使 lite 测试无法隔离 | 与现状一致；不引入新问题；未来由 Phase 3 移除 Manager 单例 |
| `RuntimeOperationLease` 锁模式与 VoicebankSession shutdown 互锁 | extractors 路径不经 session；shutdown 序列保留先 `m_session.~VoicebankSession()` 后 `m_runtime.~Runtime()`（成员析构顺序由声明顺序保证） |
| `operator SingerRef()` 隐式转换引发意外调用 | `SingerRef` 仅在 synthrt API 参数中出现，转换场景有限；编译器仅在需要 `SingerRef` 时触发；不重载 `operator==` 避免比较歧义 |
| 多版本场景下 `Manager` context 名 `packageId:singerId` 与版本无关导致冲突 | VoicebankSession 内部 context 名已包含 version（V3-10 project_memory 要求）；lite 无需关心 |
| B3 缓存层 staleness 检查可能误判 | staleness 检查基于 `snapshot->generation` 与 `m_catalogGeneration` 比较；stale 时返回空驱动 UI 重新触发 refresh，不返回 stale 数据；B3-T05 已验证 |
| B3 `SvsSingerAmbiguous` UI 弹窗未实现 | B3-T04 已验证；UI 层弹窗解析逻辑已添加 |

**回滚预案**：
- B1a 出现回归：revert commit dcb2b61a（旧 API 完整保留，m_session/session()/languageServicePtr() 移除）
- B1b 出现回归：revert commit c323aee2（需同时回退 InferEngine/PackageManager/2 个语言服务调用方的迁移；GetPhonemeNameTask B1b 未迁移，无需回退）
- **B1c 出现回归（关键节点）**：revert commit f652052f 单 commit 恢复 B1b 完成态（旧 API + 中间层文件 + adapter 类 + 旧成员 + GetPhonemeNameTask 旧调用 + initialize/shutdown 旧逻辑全部恢复）。**注意**：B3 commit d8e8626f 基于 B1c 后状态，若 revert B1c 需先 revert B3
- B3 出现回归：单独 revert commit d8e8626f（不影响 B1a/B1b/B1c）
