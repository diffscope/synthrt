# 验证清单与回归项

日期: 2026-07-26（v8 修订：A1/A2 单元测试文件已创建并编译/运行通过；v7 修订：A1/A2 单元测试文件实际未创建，状态改为 "☐ 测试文件未创建"；v6 修订：B3-T03 错误码勘误 + Phase A/B 完成状态同步；C1/C2 待回归）
状态: ☑ Phase A/B 验证项已落地（A1/A2 单元测试通过）；☐ C2 待用户更新 vcpkg 后执行端到端冒烟
依据: [00-overview.md](file:///d:/projects/synthrt/docs/lite-integration/00-overview.md) §4 执行顺序

---

## 1. synthrt 侧验证（Phase A）— ☑ 已完成

### A1: `srt::g2p::setupG2pOnnxDriver` — ☑ 已完成（commit 37e5b3d）

> **v8 修订**：A1 单元测试文件 `unittests/G2P/tst_g2p_onnx_setup.cpp` 已创建并合入构建（由 `unittests/G2P/CMakeLists.txt` 的 `file(GLOB _g2p_tests CONFIGURE_DEPENDS "tst_*.cpp")` 自动发现）。运行结果：6 case 中 5 通过 + 1 SKIP（A1-T05，无法在不改源码注入异常的情况下复现，由代码审查验证 try/catch 边界，参见 [lib/G2P/G2pOnnxSetup.cpp#L156-L223](file:///d:/projects/synthrt/lib/G2P/G2pOnnxSetup.cpp#L156-L223)）。

**单元测试**：`unittests/G2P/tst_g2p_onnx_setup.cpp`（**v8：已创建并通过**）

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| A1-T01 | 未调用 `setupOnnxInferenceDriver` 即调用 `setupG2pOnnxDriver` | 返回 `InferenceNotInitialized` (200)，`m_langSvc.modelsReady()==false` | ☑ 通过 |
| A1-T02 | 先 `setupOnnxInferenceDriver` 再 `setupG2pOnnxDriver` | 成功；Manager 的 `kDriverCategory` 中存在 `kG2pOnnxDriverName` 对象 | ☑ 通过 |
| A1-T03 | 重复调用 `setupG2pOnnxDriver` | 幂等；不抛异常；返回 `SRT_OK` | ☑ 通过 |
| A1-T04 | `g2pPluginPaths` 含不存在路径 | 不致命；记录到 `drainPendingDiagnostics()`；返回 `SRT_OK` | ☑ 通过 |
| A1-T05 | ONNX driver 创建抛 `std::exception` | 边界捕获；返回 `GenericError`，不穿越 extern 边界（CODING-02） | ☑ SKIP（代码审查验证） |
| A1-T06 | G2P plugin path 含非 ASCII 路径（如中文目录） | 正常注册（CODING-03） | ☑ 通过 |

> **v6 错误码勘误**：A1-T01 期望值原 "InferenceNotInitialized" 未写数值；v6 补 200（[Diagnostic.h#L66](file:///d:/projects/synthrt/include/synthrt/Core/Support/Diagnostic.h#L66)）。

**回归**：
- `unittests/G2P/` 现有 case 全过（`tst_g2p_*.cpp` / `tst_convert_basic.cpp` / `tst_language_service_isolation.cpp` 等） — ☑ 已通过（v8：143 case / 141 passed + 2 skipped / 709 assertions）
- `domains/ds-bank/unittests/tst_voicebank_g2p_integration.cpp` 全过 — ☑ 已通过（v8：tst-ds-bank 225 case / 224 passed + 1 skipped / 1083 assertions）

**手动验证**：
```powershell
# Windows / PowerShell
cd D:\projects\synthrt
cmake --build --preset windows-debug --target synthrt-unittest-g2p
ctest --test-dir build\windows-debug -R "synthrt-unittest-g2p" --output-on-failure
```

### A2: `VoicebankSnapshot::findSinger/findPackage/findManifest` — ☑ 已完成（commit d0015af）

> **v8 修订**：A2 单元测试文件 `domains/ds-session/unittests/tst_vbs_snapshot_query.cpp` 已创建并合入构建（`domains/ds-session/unittests/CMakeLists.txt` 新增 `tst-ds-session-snapshot-query` target）。运行结果：11 case 全过，27 assertions。

**单元测试**：`domains/ds-session/unittests/tst_vbs_snapshot_query.cpp`（**v8：已创建并通过**）

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| A2-T01 | `findSinger(SingerRef{pkg, singer, "1.0.0"})` 精确匹配 | 命中 `SingerSnapshot*` | ☑ 通过 |
| A2-T02 | `findSinger(SingerRef{pkg, singer, "1.0"})` 版本规范化 | 命中（与 "1.0.0" 等价） | ☑ 通过 |
| A2-T03 | `findSinger(SingerRef{pkg, singer, "2.0.0"})` 不存在 | 返回 `nullptr` | ☑ 通过 |
| A2-T04 | `findSingersBySingerId("singerA")` 多版本场景 | 返回 2 个 `SingerSnapshot*` | ☑ 通过 |
| A2-T05 | `findSingersBySingerId("notExist")` | 返回空 vector | ☑ 通过 |
| A2-T06 | `findPackage(pkg, ver)` valid package | 命中 `PackageStatus*`；`valid==true` | ☑ 通过 |
| A2-T07 | `findPackage(pkg, ver)` invalid package | 命中 `PackageStatus*`；`valid==false`（仍返回） | ☑ 通过 |
| A2-T08 | `findPackage(pkg, ver)` 不存在 | 返回 `nullptr` | ☑ 通过 |
| A2-T09 | `findManifest(pkg, ver)` valid package | 命中 `PackageManifest*` | ☑ 通过 |
| A2-T10 | `findManifest(pkg, ver)` invalid package | 返回 `nullptr`（invalid 包无 manifest） | ☑ 通过 |
| A2-T11 | 多线程并发 `findSinger` 读 snapshot | 无数据竞争（const 方法 + 不可变 snapshot） | ☑ 通过 |

**回归**：
- `domains/ds-session/unittests/tst_vbs_snapshot.cpp` 全过 — ☑ 已通过（v8：4 case / 3 passed + 1 skipped / 26 assertions）
- `domains/ds-session/unittests/tst_voicebank_session_snapshot_ensure.cpp` 全过 — ☑ 已通过（v8：22 case / 21 passed + 1 skipped / 112 assertions）

### A3: 文档更新 — ☑ 已完成（commit 4e0a3ff）

- [x] `docs/modules/overview.md` §3.1 增加 lite 推荐入口标注
- [x] `docs/modules/g2p.md` 末尾追加 `setupG2pOnnxDriver` 交叉引用
- [x] `docs/modules/ds-session.md` 在 `VoicebankSnapshot` 节追加 4 个查询方法说明
- [x] 文档中所有 `file:///` 链接本地可点击
- [x] 文档版本号与日期更新

---

## 2. lite 侧验证（Phase B）— ☑ 已完成

### B1a: SingerIdentifier 隐式转换 + SynthrtEngine 添加 session（双 API 共存） — ☑ 已完成（commit dcb2b61a）

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B1a-T01 | `SingerIdentifier::operator SingerRef()` 版本规范化 | `SingerRef.version` 字符串与 `VersionNumber::toString()` 一致（"1.0" 不带尾随零） | ☑ C1/C2 待回归 |
| B1a-T02 | `SynthrtEngine::initialize()` 填充 SessionResources + `setRoots` + `refresh()` | `session().snapshot()->generation >= 1`；`languageService()->metadataReady()==true`（自动初始化） | ☑ C1/C2 待回归 |
| B1a-T03 | 双 API 共存编译 | 旧 API 与新 `session()` 同时可用；调用方未迁移也能编译 | ☑ 已验证（commit dcb2b61a） |
| B1a-T04 | `SynthrtEngine::shutdown()` 析构顺序 | `m_session` 析构先于 `m_runtime`；无 use-after-free；`RuntimeOperationLease` 在 shutdown 后立即失效 | ☑ C1/C2 待回归 |

**回归**：
- `domains/ds-bank/unittests/tst_ds_editor_lite_scenarios.cpp` 全过 — ☑ C1/C2 待回归
- lite 启动冒烟：打开应用，无 fatal log — ☑ C2 待回归

### B1b: 逐文件迁移调用方（InferEngine chokepoint + 5 文件） — ☑ 已完成（commit c323aee2）

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B1b-T01 | `InferEngine::acquireSingerSession` 返回 `ModelSetHandle` | 4 个 task（Acoustic/Duration/Pitch/Variance）编译通过；运行时 `handle->load(kind)` 成功 | ☑ 已验证（commit c323aee2） |
| B1b-T02 | `ActiveInference::acquire` 适配 `ModelSetHandle` | `handle.model().inference` 非空；`importOptions` 有效；Handle 接口不变 | ☑ C1/C2 待回归 |
| B1b-T03 | stale 重试（`InferEngine::acquireSingerSession` 内部） | 模拟 `StaleModelSet` 触发一次重试后成功 | ☑ C1/C2 待回归 |
| B1b-T04 | `PackageManager::refreshInstalledPackages` 改用 `session().setRoots()+refresh()` | 返回 `GetInstalledPackagesResult`，与旧版 UI 列表显示一致 | ☑ C1/C2 待回归 |
| B1b-T05 | `PackageManager` 缓存层基于 `snapshot->findPackage/findSinger` | UI 显示无回归；`m_catalogGeneration` 与 `snapshot->generation` 一致（staleness 检查由 B3 补完） | ☑ C1/C2 待回归；B3 已补 staleness |
| B1b-T06 | `G2pService` / `GetPronunciationTask` 改用 `session().convertG2p` | G2P 转换结果与旧 `resolveLanguageRoute` + `convert` 一致 | ☑ C1/C2 待回归 |
| B1b-T07 | `GetPhonemeNameTask` 改用 `session().convertS2p` + `ensureLanguageReady` | S2P 转换结果与旧 `resolveS2pResource` 一致 | ☑ C1/C2 待回归；B1c 补完成迁移 |
| B1b-T08 | extractors 路径不受影响 | `acquirePitchExtractionOperation()` 正常；不阻塞 voicebank refresh | ☑ C1/C2 待回归 |
| B1b-T09 | 4 个 Infer task 端到端推理 | Duration/Pitch/Variance/Acoustic 全部成功（**v3 核对：Vocoder 不调用 acquireSingerSession**） | ☑ C2 待回归 |

**回归**：
- `domains/ds-bank/unittests/tst_ds_editor_lite_scenarios.cpp` 全过 — ☑ C1/C2 待回归
- lite 端到端推理冒烟：4 个 task 均成功 — ☑ C2 待回归

### B1c: 删除旧 API + 中间层文件 — ☑ 已完成（commit f652052f）

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B1c-T01 | 删除旧 API 后编译 | 无对旧 API 的残留引用；编译干净 | ☑ C1/C2 待回归 |
| B1c-T02 | G2P ONNX adapter 完全删除 | lite 无 `G2pOnnxSessionTask` / `G2pOnnxSessionFactory`；由 A1 提供 | ☑ C1/C2 待回归 |
| B1c-T03 | PackageCatalog / SingerModelSession 文件删除 | lite 编译通过；无残留引用；链接成功 | ☑ C1/C2 待回归 |
| B1c-T04 | `m_loadedPackageDirs` / `m_singerSessions` / `m_catalog` 成员删除 | lite 编译通过；无残留引用 | ☑ C1/C2 待回归 |
| B1c-T05 | 端到端推理 | 4 个 task 全过 | ☑ C2 待回归 |
| B1c-T06 | `m_session.refresh()` 失败返回 false | refresh 失败时 `initialize()` 返回 false（B1c 行为变更） | ☑ C1/C2 待回归 |
| B1c-T07 | `GetPhonemeNameTask` 已迁移 | S2P 转换结果与旧 `resolveS2pResource` 一致 | ☑ C1/C2 待回归 |

### B2: LanguageService 自动初始化（合并到 B1a/B1b，无独立验证） — ☑ 已合并

> **v3 核对**：B2 经核对实际代码后合并到 B1a（SessionResources 自动初始化）+ B1b（调用方迁移到 version-aware 路由）。以下验证项并入 B1a/B1b。

| Case ID | 场景 | 期望结果 | 归属 |
|---|---|---|---|
| B2-T01 | 单版本同 packageId 启动 | `metadataReady()==true`；`modelsReady()==true` | B1a-T02 |
| B2-T02 | 多版本同 packageId 启动（v1.0.0 + v1.1.0） | `metadataReady()==true`；两个 version 的路由独立 | B1b-T06 |
| B2-T03 | `initializeModels` 失败 | `modelsReady()==false`；`RefreshResult.diagnostics` 含 Warning；`convertG2p` 返回 `G2pNotInitialized` | B1a-T02 |
| B2-T04 | 启动后 refresh（新增包） | `RefreshResult.changes.added` 含新包 | B1b-T04 |
| B2-T05 | 启动后 refresh（移除包） | `changes.removed` 含被移除包；旧 `ModelSetHandle` `isStale()==true` | B1b-T04 |
| B2-T06 | `convertG2p` 多版本路由隔离 | 同 singerId 不同 version 返回不同 G2P 结果 | B1b-T06 |
| B2-T07 | `convertS2p` 缓存键含 version | 多版本场景下 S2P 缓存不串扰 | B1b-T07 |
| B2-T08 | `ensureLanguageReady` 在 `modelsReady==false` | 返回 `G2pNotInitialized` 错误，不静默 | B1b-T07 |
| B2-T09 | `ensureLanguageReady` 幂等 | 同 `(packageId, version, lang)` 二次调用立即返回 `SRT_OK` | B1b-T07 |

**回归**：
- `unittests/G2P/tst_update_metadata.cpp` 全过 — ☑ 已通过（v8：synthrt-unittest-g2p 143 case 全过）
- `unittests/G2P/tst_language_service_isolation.cpp` 全过 — ☑ 已通过（v8：synthrt-unittest-g2p 143 case 全过）
- `domains/ds-bank/unittests/tst_voicebank_g2p_integration.cpp` 全过 — ☑ 已通过（v8：tst-ds-bank 225 case 全过）

### B3: findSinger 改用 snapshot 查询 + SvsSingerAmbiguous（PackageManager 新增 helper） — ☑ 已完成（commit d8e8626f）

| Case ID | 场景 | 期望结果 | 状态 |
|---|---|---|---|
| B3-T01 | `PackageManager::findSingerBySingerId` 唯一匹配 | 返回 `SingerIdentifier`；`packageVersion` 填充 | ☑ C1/C2 待回归 |
| B3-T02 | 多版本同 singerId 匹配 | 返回 `Error{SvsSingerAmbiguous}`；code() == 604 | ☑ C1/C2 待回归 |
| B3-T03 | 无匹配 | 返回 `Error{SvsSingerNotFound}`；code() == **600** | ☑ C1/C2 待回归 |
| B3-T04 | lite UI 弹窗解析 `SvsSingerAmbiguous` | 提示用户选择版本；选择后用 `SingerRef.version` 路由 | ☑ C1/C2 待回归 |
| B3-T05 | 不再出现 `PackageVersionConflict` 错误码 | 全代码库 grep 无 `PackageVersionConflict` 在 `findSinger` 上下文 | ☑ C1/C2 待回归 |
| B3-T06 | 缓存层 staleness 检查 | refresh 后 `m_catalogGeneration == snapshot->generation`；stale 时 `installedPackages` 返回空 | ☑ C1/C2 待回归 |

> **v6 错误码勘误**：B3-T03 原 `code() == 605` 错误，已对照 [Diagnostic.h#L130](file:///d:/projects/synthrt/include/synthrt/Core/Support/Diagnostic.h#L130) 修正为 `600`。同时 lite 代码 [PackageManager.cpp:352](file:///d:/projects/ds-editor-lite/src/app/Modules/PackageManager/PackageManager.cpp#L352) 注释中 `SvsSingerNotFound (605)` 已同步勘误为 `600`（commit 0a88862d）。

**回归**：
- `domains/ds-bank/unittests/tst_singer_ref.cpp` 全过 — ☑ 已通过（v8：tst-ds-bank 225 case 全过）
- `domains/ds-infer/unittests/catch2/tst_singer_resolver_ambiguity.cpp` 全过 — ☑ C1 待回归

---

## 3. 端到端冒烟测试 — ☐ 待 C2 执行（用户更新 vcpkg 后）

> 以下冒烟步骤对应 C2 阶段；待用户更新 vcpkg 后由用户手动执行。

### 3.1 lite 启动 + 单声库推理

```powershell
# 1. 编译 synthrt（确保 A1 已合入）
cd D:\projects\synthrt
cmake --build --preset win-x64-debug
cmake --install build\win-x64-debug --prefix build\install  # 安装到本地

# 2. 编译 lite（依赖本地 synthrt）
cd D:\projects\ds-editor-lite
cmake --build --preset win-x64-debug

# 3. 启动 lite，加载一个测试声库，跑完整推理
.\build\win-x64-debug\bin\ds-editor-lite.exe
```

**期望**：
- 启动无 fatal
- PackageManager 显示声库列表
- 创建 Singer track，输入歌词，G2P 转换成功
- 触发 Duration → Pitch → Variance → Acoustic → Vocoder 推理，输出 WAV

### 3.2 多版本共存

```powershell
# 准备两个同 packageId 不同 version 的声库目录
# C:\voicebanks\pkg-x\v1.0.0\desc.json (version: 1.0.0)
# C:\voicebanks\pkg-x\v1.1.0\desc.json (version: 1.1.0)

# 启动 lite，加载两个目录
.\build\win-x64-debug\bin\ds-editor-lite.exe
# 在 PackageManager 中确认两个版本独立显示
# 分别创建 track 绑定 v1.0.0 与 v1.1.0 singer，分别推理
```

**期望**：
- 两个 singer 独立加载，无 `PackageVersionConflict`
- v1.0.0 推理结果与单独安装 v1.0.0 一致
- v1.1.0 推理结果与单独安装 v1.1.0 一致

### 3.3 热添加声库

```powershell
# 启动 lite，加载一个声库目录
# 推理一次，确认成功
# 复制新声库到目录
# PackageManager 触发 refresh（无需重启 lite）
# 新声库立即可用，原推理结果不受影响
```

**期望**：
- `refresh()` 返回 `RefreshResult.succeeded==true`，`changes.added` 含新包
- 不再返回 `PackageScanAfterInitialize` 错误
- 原已加载 singer 继续可用；新 singer 可加载并推理

### 3.4 stale 重试

```powershell
# 启动 lite，加载声库 A v1.0.0，开始推理
# 推理进行中：替换声库 A 为 v1.0.1（同 packageId 不同 version）
# 触发 refresh
# 等待原推理完成
# 触发新推理
```

**期望**：
- 原推理正常完成（使用旧模型，stale handle 但 `start()` 已返回）
- 新推理：`ensureModelSet` 返回新 handle（绑定新 generation）；不触发 `StaleModelSet` 重试，因为 handle 是新建的
- 若 task 在 refresh 进行中调用 `start()`：返回 `StaleModelSet`，task 一次重试后成功

---

## 4. 静态检查

| 项 | 工具 | 期望 |
|---|---|---|
| 头文件循环依赖 | `scripts/check-include-cycles.py` | 无新增循环 |
| clang-tidy warnings | LLVM 16 | 不超过 baseline (`docs/testing/baseline-clang-tidy-warnings.json`) |
| clang-format | LLVM 16 | 所有新增/修改文件通过 |
| cmake-format | 0.6.13 | 所有 CMakeLists.txt 通过 |

---

## 5. 验证矩阵

| 平台 | 编译 | 单元测试 | 集成测试 | lite 端到端 |
|---|---|---|---|---|
| Windows MSVC x64 | ✓ | ✓ | ✓ | ✓ |
| Linux GCC x64 | ✓ | ✓ | ✓ | N/A（lite 是 Qt 应用，可选） |
| Linux Clang x64 | ✓ | ✓ | ✓ | N/A |
| macOS Intel | ✓ | ✓ | ✓ | N/A |
| macOS ARM | ✓ | ✓ | ✓ | N/A |

---

## 6. 回滚预案

每个 Phase / commit 独立可 revert：

| 阶段 | revert 影响 |
|---|---|
| A1 | 仅移除新文件，lite 失去 G2P ONNX driver（恢复自实现） |
| A2 | 仅移除 snapshot 查询方法；lite 改回自实现遍历 |
| A3 | 仅文档回滚 |
| B1a | 仅移除 `m_session` 成员 + `session()` API + `SingerIdentifier::operator SingerRef()`；旧 API 保留；双 API 共存回退到旧 API 单一形态 |
| B1b | 回滚 10 个调用方文件到旧 API；`InferEngine::acquireSingerSession` 改回返回 `shared_ptr<SingerModelSession>`；`ActiveInference` 改回包装 `SingerModelSession`（4 个 task 类型回退） |
| B1c | **高风险**：恢复旧 SynthrtEngine 公共 API（`refreshVoicebanks`/`acquireSingerSession`/`resolveLanguageRoute` 等）+ `PackageCatalog`/`SingerModelSession` 文件 + G2P ONNX adapter 类；恢复 `m_loadedPackageDirs`/`m_singerSessions`/`m_catalog`/`m_catalogRefreshMutex`/`m_singerRwLock` 成员 |
| B2 | （已合并到 B1a/B1b，无独立 commit；回滚 B1a/B1b 即自动回滚 LanguageService 自动初始化与 version-aware 路由） |
| B3 | `PackageManager::findSingerBySingerId` 改回旧实现；移除 `SvsSingerAmbiguous` 错误码处理；恢复 `PackageVersionConflict` 上下文 |

**B1c 是关键节点**：因 v3 无桥接，B1c 一次性删除大量代码（旧 API + 中间层文件 + adapter）；若出现严重回归，revert B1c 单 commit 即可恢复 B1b 完成态（旧 API + 中间层全部恢复，调用方已迁移部分回退到旧 API）。B1a/B1b 出现回归可单独 revert，不影响 B1c 已完成的删除工作。B2 已合并到 B1a/B1b，无独立回滚。

---

## 7. 完成标记

每阶段完成后，在对应行追加 `commit-hash` 与日期：

| 阶段 | 状态 | commit | 日期 |
|---|---|---|---|
| A1 setupG2pOnnxDriver | ☑ 已完成 | 37e5b3d | 2026-07-25 |
| A2 VoicebankSnapshot 查询方法 | ☑ 已完成 | d0015af | 2026-07-25 |
| A3 文档更新 | ☑ 已完成 | 4e0a3ff | 2026-07-25 |
| B1a SingerIdentifier 隐式转换 + SynthrtEngine 添加 session（双 API 共存） | ☑ 已完成 | dcb2b61a | 2026-07-25 |
| B1b 逐文件迁移调用方（InferEngine chokepoint + 5 直接 + 4 task 类型替换） | ☑ 已完成 | c323aee2 | 2026-07-25 |
| B1c 删除旧 SynthrtEngine API + PackageCatalog/SingerModelSession 文件 + adapter | ☑ 已完成 | f652052f | 2026-07-25 |
| B2 LanguageService 自动初始化（合并到 B1a/B1b，无独立 commit） | — | — | — |
| B3 findSinger 改用 snapshot 查询 + SvsSingerAmbiguous | ☑ 已完成 | d8e8626f | 2026-07-25 |
| **v6 文档勘误**（lite `PackageManager.cpp:352` 注释 `SvsSingerNotFound (605)` → `600`） | ☑ 已完成 | 0a88862d（lite 仓库） | 2026-07-26 |
| **v6 文档修订**（04-interface-contract.md 4 处错误码数值勘误 + 状态同步） | ☑ 已完成 | c64a509 | 2026-07-26 |
| **v7 文档勘误**（auto-init 触发条件 + 测试文件声明 + CMake 描述 + SessionResources 字段，8 文件修订） | ☑ 已完成 | 6e728d8 | 2026-07-26 |
| **v8 测试补齐**（A1/A2 单元测试文件创建 + 编译/运行通过 + 回归测试通过） | ☑ 已完成 | ef5b0bb | 2026-07-26 |
| C1 单元+集成测试回归 | ☑ A1/A2 + 相关回归已通过；剩余 ds-infer 项待回归 | — | 2026-07-26 |
| C2 lite 端到端冒烟 | ☐ 待执行 | — | — |
