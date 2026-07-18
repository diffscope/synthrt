# SynthRT v3 深化重构计划

日期: 2026-07-18

定位: 本计划在 [vNext](../refactoring-vnext/) 基础上深化，重点解决 vNext 未触及的遗留问题——**LanguageService 版本隔离**、Session API 不彻底、快照指纹缺失等。所有方案均以 ds-editor-lite 调用方真实需求为驱动，以接口抽象稳定、长期兼容、间接可读、不过度设计为原则。

## 与既有文档的关系

- **继承** [human-decisions.md](../decisions/human-decisions.md) D-01~D-39 全部约束
- **修订** [refactoring-vnext/](../refactoring-vnext/) 中与 D-26~D-39 不一致的部分
- **深化** [refactoring-vnext/06-implementation-roadmap.md](../refactoring-vnext/06-implementation-roadmap.md) 中 LanguageService 版本隔离（D-37 升级为短期实施）

## 范围与边界

| 范围内 | 范围外 |
|---|---|
| LanguageService 全链路版本隔离（4 层改造） | Lite 生产路径迁移（Phase 2 实施） |
| VoicebankSession API 收敛 + `SessionResources` 注入 | Lite 适配器代码改写 |
| Snapshot 指纹字段（`catalogFingerprint`/`languageFingerprint`） | Lite UI 改造 |
| `ensure*` 同步 API + 结构化错误码 | 缓存 LRU 回收策略 |
| 多版本路由歧义显式拒绝（`G2PVersionAmbiguous`） | 跨 host 跨进程协议 |
| C ABI 新增 `srt_session_create_with_resources` | 已发布 C ABI 签名变更（破坏性） |
| Stale 一次重建 + 一次重试契约 | 模型缓存写回路径调整 |

## 核心决策摘要

| # | 决策 | 对应人工决策 |
|---|---|---|
| V3-01 | LanguageService **5 层**版本隔离（含 VoicebankScanner.packageDirectory），使用现有 `G2pInput.g2pContext`/`g2pContextVersion` 字段路由，无需破坏性变更 | 升级 D-37 |
| V3-02 | G2P Manager context 命名约定: `packageId__singerId` + voicebank version（非 G2P 子包版本），符合 PackageManager R-8/R-9 双字段规则（`__` 而非 `:`，因 `validateContextName` 禁用 `:`——保留用于 FQID 分隔） | 新增 |
| V3-03 | `LanguageService` 新增 `PackageDirectoryEntry` 类型替代 `unordered_map<packageId, path>` | 新增 |
| V3-04 | `VoicebankScanner` 新增 `packageDirectories(packageId)` 返回 `vector<(version, path)>`，旧 `packageDirectory(id)` 标记 deprecated | 新增（核实后发现） |
| V3-05 | 旧 `initializeMetadata(map)` 与 `resolveLanguageRoute(无 version)` 重载标记 `[[deprecated]]`，保留实现，下一 Level 删除 | D-11, D-37 |
| V3-06 | `VoicebankSession` 新增 `SessionResources` 注入构造，bare setter 标记 deprecated | D-27 |
| V3-07 | Snapshot 新增 `catalogFingerprint`/`languageFingerprint`，与 Lite `PackageCatalog::Snapshot` 对齐 | D-33 |
| V3-08 | 新增 `ensureLanguageReady(packageId, version, language)` 与 `ensureModelSet(SingerRef)` 同步 API | D-34 |
| V3-09 | 新增错误码：`G2PVersionAmbiguous=321`、`LoadFailed=217`、`RuntimePackageNotLoaded=218`（数值已核实，避免与现有码冲突） | D-32, D-34 |
| V3-10 | 多版本路由策略: caller 提供版本→精确路由；caller 不提供→单版本用唯一/多版本返回歧义错误 | D-32 |
| V3-11 | Discovery 部分成功: 合法包入 snapshot，损坏包入 diagnostics，整体仍返回成功 | D-31 |
| V3-12 | Stale 重试契约: 丢弃旧 handle → 重建一次 → 重试一次 → 仍失败则报错，不取消正在运行的任务 | D-30 |
| V3-13 | C ABI 新增 `srt_session_create_with_resources`，保留 `srt_session_create` 默认构造 | D-36 |
| V3-14 | Lite 迁移本轮仅约定接口契约，不实施适配器代码改写 | D-28 |
| V3-15 | 跨平台/编码约束：所有跨边界路径序列化必须经 `stdc::path::to_utf8()`，禁止 `path.string()` | 新增 |
| V3-16 | 热重载（可选 WP8）：`LanguageService::updateMetadata` 增量 API + `PackageManager::removeContextsByPrefix` 清理；全量 initializeMetadata 仍可工作 | 新增 |
| V3-17 | Runtime unload 不在本轮（Phase 3），旧包内存暂留不影响功能 | 新增 |

## 文档导航

- [01 目标与原则](01-goals-and-principles.md) — 范围、原则、与人工决策对齐
- [02 LanguageService 版本隔离](02-language-service-version-isolation.md) — 核心深化设计（5 层全链路）
- [03 Session 与快照](03-session-and-snapshot.md) — VoicebankSession API、Snapshot 指纹、`ensure*`、Stale
- [04 多版本路由与错误码](04-routing-and-errors.md) — 路由策略、歧义处理、错误码表
- [05 C ABI 与迁移测试](05-c-abi-and-migration.md) — C ABI 扩展、Lite 迁移规则、测试策略
- [06 核实、跨平台、编码、热重载](06-verification-cross-platform-hotreload.md) — 反向核实结果、跨平台约束、热重载增量 API

## 实施顺序

```
WP0 错误码新增（数值已核实）
  ↓
WP1 LanguageService 版本隔离（5 层，含 VoicebankScanner.packageDirectories）
  ├─> WP2 Snapshot 指纹
  ├─> WP3 Session API 收敛
  ├─> WP6 C ABI 扩展
  └─> WP8 热重载（可选，增量 updateMetadata）
       │
WP4 ensure* API + Stale 契约 ──> WP5 多版本路由歧义 ──> WP7 同步合同测试
```

每个 WP 单独提交（对齐 D-24），不推送；完成后在本 README 末尾追加状态行。

## 实施状态

| WP | 状态 | 说明 |
|---|---|---|
| WP0 | ✅ 完成 | 错误码新增（`G2PVersionAmbiguous=321` 等） |
| WP1 | ✅ 完成 | LanguageService 5 层版本隔离（含 VoicebankScanner.packageDirectories）。5 层全部消除版本丢失：initializeMetadata 入口层、resolveLanguageRoute 路由层、G2P Manager context 层、S2P cache 层、convert 层。context 分隔符由 `:` 改为 `__`（`validateContextName` 约束）。新增 11 个测试（7 G2P 隔离 + 4 VoicebankScanner 多版本），修复 10 个既有测试断言以反映 V3-01 行为变更。全套测试通过（synthrt-unittest-g2p: 56 assertions/12 cases；tst-ds-bank: 684 assertions/139 cases）。 |
| WP8 | ✅ 完成（G2P 核心 + session 增量） | G2P 核心热重载 API（commit 4b4bf8c）：`LanguageService::updateMetadata` 增量计算 `PackageDirectoryDiff`，注册 added voicebank G2P context、通过 `PackageManager::removeContextsByPrefix` 移除 retired context、失效 manifest/S2P 缓存；`PackageManager::removeContextsByPrefix` 按 prefix 清理 Pending 状态的 context 及关联状态。热重载限制：modelsReady 后调用返回 `G2pAlreadyInitialized`。VoicebankSession::refresh 增量改造由 WP8-session 落地（commit 3c576a10）：在 `performRefresh()` 的 no-change 早返回之后、publish 之前插入 `updateMetadata` 调用，失败时 fallback 到 `initializeMetadata`，再失败仅记录 Warning 诊断而不阻断 snapshot 发布（符合 06 文档 §4.2.5）。 |
| WP6 | ✅ 完成 | C ABI 扩展。新增 `srt_RuntimeHandle`/`srt_LanguageServiceHandle` 不透明句柄类型，`srt_runtime_create/destroy`、`srt_language_service_create/destroy` 生命周期函数，以及 `srt_session_create_with_resources` 借用式资源注入工厂。HandleTable 新增 `RuntimeData`/`LanguageServiceData` 表项与编解码辅助。资源句柄为 caller-owned；session 通过非拥有 aliasing shared_ptr 借用（WP3 落地 SessionResources 注入构造后切换）。错误码沿用现有 `srt_error` enum（V3-09 新增的 C++ ErrorCode 经 `mapErrorCode` 映射，不进入 C ABI enum）。保留 `srt_session_create_v2`。 |
| WP2 | ✅ 完成 | VoicebankSnapshot 指纹字段（commit ec30da2）。新增 `catalogFingerprint`（包目录内容指纹）/`languageFingerprint`（语言路由内容指纹）字段及 `fingerprintPackageLanguage`/`computeCatalogFingerprint`/`computeLanguageFingerprint` 三个辅助函数；修复 `fingerprintPackage` 中 `generic_string()` → `stdc::path::to_utf8()`（V3-15）；`changed` 判定改用指纹比较；修正任务 spec 中 `previous->languageFingerprint` 自比较笔误为 `next->languageFingerprint`。 |
| WP3 | ✅ 完成 | SessionResources 注入构造（commit 26941114）。新增 `SessionResources` 结构体与 `explicit VoicebankSession(SessionResources)` 构造；`setLanguageService`/`setRuntime` 加 `[[deprecated]]` 标记。 |
| WP4 | ✅ 完成 | ensure* 同步 API（commit 6fd5e08a）。新增 `ensureLanguageReady(packageId, version, language)`：含 `G2pVersionAmbiguous`/`G2pPackageNotFound`/`RuntimePackageNotLoaded`/`G2pInitializationError`/`LoadFailed` 错误分类与懒加载 `initializeModels`；新增 `ensureModelSet(singerKey)` 薄包装 `createModelSet`。 |
| WP5 | ✅ 完成 | 多版本路由（commit 3ab2d62e）。`convertG2p` 改用 5-arg 版本感知 `svc->convert(packageId, version, singerId, language, inputs)`，version 由 `stdc::VersionNumber::fromString(singerKey.version)` 解析。 |
| WP8-session | ✅ 完成 | 见 WP8 行 session 部分（commit 3c576a10）。 |
| WP7 | ✅ 完成 | 同步合同测试（commit d623fd9）。在 `tst-ds-session-hardening` 追加 15 个 V3 同步合同 TEST_CASE（`[ds-session][v3]` 标签）：A. ensure* 错误路径（RuntimePackageNotLoaded/G2pNotImplementedError/G2pPackageNotFound/InferenceNotInitialized/SvsSingerNotFound）；B. Snapshot fingerprint 稳定性与变化检测；C. G2PVersionAmbiguous 多版本路由（L2 SKIP）；D. Stale 一次重建+一次重试（L2 SKIP）；E. updateMetadata 热重载限制（L2 SKIP，§4.2.5）；F. 多版本 fixture 占位（L2 SKIP）。L1 共 8 通过；L2 共 7 个 SKIP 占位。全套 121 assertions / 29 cases（20 passed / 9 skipped）无回归。 |
| S2P romaji bug | ✅ 完成 | 修复假名→罗马音→S2P 链路尾随空格 bug（commit 5e49112）。`DictStep::handle` 改为标准 join（无尾随空格）；`DirectS2P::convert` 增加防御性空 token 跳过，避免任何调用方传入含空格字符串时切出空 token 导致 `MappingS2P` 查表失败。 |
| D-31 partial success | ✅ 完成 | 修复 Discovery 阶段违反 D-31/V3-11 的"全 or 全不"中止行为（commit a778e68）。`VoicebankSession::performRefresh` 移除遇到首个损坏包即中止整个 refresh 的循环；改为过滤无效包后只发布有效包到 snapshot，损坏包诊断信息通过 `diagnostics` 返回。仅当扫描到包但全部无效时才返回 `succeeded=false`（保留原行为）。更新 4 个固化旧中止行为的测试为断言 D-31 部分成功契约。全套测试通过（tst-ds-session: 92 assertions/23 cases；tst-ds-session-hardening: 133 assertions/22 passed/9 skipped）。 |
| G2P 基础测试补充 | ✅ 完成 | 为 G2P 模块补充 3 个基础测试文件（commit 8bae688 + 6a913d4）。`unittests/G2P/test_update_metadata.cpp`：6 个 TEST_CASE 覆盖 V3-16 热重载（add/remove/no-change diff、modelsReady 后 G2pAlreadyInitialized、未 initializeMetadata 时 G2pInitializationError、removeContextsByPrefix 验证）；`test_convert_basic.cpp`：6 个 TEST_CASE 覆盖 readiness 状态转换、convertLyric 在 modelsReady=false 下返回 failed、convert 未知 packageId 返回 G2pPackageNotFound、convert 不支持语言返回 G2pValidationError、resolveS2pResource 合法/非法输入、resolveLanguageRoute 官方 G2P 路由；`test_resolve_s2p_resource.cpp`：5 个 TEST_CASE 覆盖 S2P 缓存命中（同 shared_ptr）、不同 singer/不同 language 独立缓存、空 packageId/未知 singer 错误路径、dict mode convert 音素返回。全套 31 cases / 30 passed / 1 skipped（L2 deferral：Manager::initialize 需真实 ONNX 模型）。 |
| Bug 2: convertS2p 版本隔离 | ✅ 完成 | 修复 `convertS2p` 版本隔离缺失（commit 2018abb）。`convertS2p` 原使用 3-arg `resolveS2pResource`（无 version 参数），在多版本同 packageId 场景下路由不精确，与 `convertG2p` 不对称。新增 4-arg 版本感知 `resolveS2pResource` 重载，经 5-arg `resolveLanguageRoute` 路由并按 `(packageId, version, singerId, languageId)` 缓存；旧 3-arg 重载标记 `[[deprecated]]` 并委托到 4-arg。`convertS2p` 现从 `SingerRef.version` 解析 `VersionNumber` 并调用新重载，与 `convertG2p` 模式一致。新增 2 个测试（歧义路径 + 显式版本路由）。全套测试通过（tst-ds-session-hardening: 33 cases/24 passed/9 skipped/149 assertions；synthrt-unittest-g2p: 31 cases/30 passed/1 skipped/159 assertions）。 |
| Bug 3: computeChanges disabled 去重 | ✅ 完成 | 修复 `computeChanges` 在两个 singer 共享同一 PackageCoordinate 时产生重复 `disabled` 条目（commit b932eec）。在 `computeChanges` 末尾增加去重段落：使用 `std::set<std::string>` + 现有 `coordinateKey` 辅助函数构建字符串键进行去重，保持首次出现顺序，无需为 `PackageCoordinate` 新增 `operator<`。新增测试 `computeChanges does not duplicate disabled entries for singers sharing coordinate`：构造同包双歌手（无 imports 使其 Available）→ 切换 roots 移除包 → 断言 `disabled` 中 `session.dup` 仅出现一次。全套测试通过（tst-ds-session-hardening: 34 cases/25 passed/9 skipped/157 assertions）。 |
| ds-bank 边缘场景测试补充 | ✅ 完成 | 结合 `ds-editor-lite` PackageCatalog/SynthrtEngine 真实使用场景，为 `domains/ds-bank` 补全 5 个测试文件（commit 1dcf104）：`test_voicebank_scanner_edge_cases.cpp`（27 cases，覆盖 v-prefix 版本号、5+/4 段版本号、自依赖、2/3 节点循环依赖、diamond 依赖、缺失依赖、Unicode 路径、增量热更新、深层路径、空 version、重复 dependencies、同 packageId+version 跨目录合并）；`test_ds_editor_lite_scenarios.cpp`（13 cases，模拟 PackageCatalog fingerprint 不可变性、新增/升级包改变指纹、跨路径同 packageId+version 检测、重复 singer identifier 检测、findSinger 跨包查找、singerSnapshot 精确版本查询、多版本同 packageId 共存、错误包不阻塞解析、空搜索路径、不存在路径跳过、allowReuse 模拟）；`test_package_list_config.cpp`（11 cases，覆盖 PackageListConfig load/save round-trip、"id[version]" 解析合法/非法、文件不存在/损坏 JSON/非数组根/空数组、缺失必填字段跳过、metadata 默认值、保序、Unicode id、save 错误路径）；`test_singer_ref.cpp`（SingerRef parse/toString/三参数构造）；`test_package_validator_manifest.cpp`（PackageValidator::validate manifest 接口的 packageId/name/version/singerId 缺失检测）。源码观察（未修复）：`PackageListItem::_version` 为 protected 且无公开 `version()` 访问器，测试通过 save 后读取原始 JSON 间接验证版本字段。全套测试通过（tst-ds-bank: 205 cases / 987 assertions，全部 PASS）。遵循 ROBUST-01/ROBUST-05/INFRA-03/CODING-03 设计原则，仅新增测试不改源码。 |
| core-g2p-s2p-cabi 跨模块边缘场景测试补充 | ✅ 完成 | 结合 `ds-editor-lite` SynthrtEngine/G2pService/FillLyric 真实使用场景，为 Core/G2P/S2P/Driver/C ABI 模块补全 9 个测试文件（commit d75ab33，9 files/2360 insertions）。**Core**：`test_version_utils_extreme.cpp`（D-18 5+段版本号不栈溢出、v/V 前缀剥离、'-' 截断、空串、VersionRange 多约束交集/reversed hyphen 空匹配/parseError/COMPATIBLE ~1.2.3 与 ~1/单操作符前缀/Constraint::matches 边界值/声库版本过滤真实场景）；`test_dependency_graph_extreme.cpp`（空图/单节点/两节点+三节点循环/自依赖 buildGraph 内部跳过/diamond 拓扑/缺失依赖 false/多 packageId/clear 重置/LevelCompatibilityChecker getEffectiveMaximumLevel+checkCorePlugin+checkDependencyPlugin+isInSupportedRange 边界+checkAll 批量+generateReport 文本+默认 LevelConfig）。**G2P**：`test_g2p_constants.cpp`（D-17/D-20 跨项目契约常量锁定：kG2pOnnxDriverName="g2pOnnxDriver"、kOfficialContext=""、kG2pSourceOfficial/kG2pSourceVoicebank、框架 category 名、G2pRes mode 常量、Plugin IID、G2pInput/G2pRes 默认值、G2pErrorType 枚举稳定性、LanguageRoute 默认值）；`test_g2p_error_migration.cpp`（扩展 4 个 TEST_CASE：typeFromCode 映射 G2pSuccess→NoError、G2P 失败码→非 NoError、g2p::Error 与 core::Error 同 ErrorCode 产生同 _type、typeFromCode 保留 None 为 NoError；验证 ER-02 修复与 slicing 安全）。**S2P**：`test_s2p_strategies.cpp`（DirectS2P 空格切分边界 8 cases：空串/前导/尾随/连续空格/单 token/internal tabs/move 语义；DictionaryS2P 11 cases：空流/合法 TSV/CRLF/missing tab/multiple tabs/empty pronunciation/empty phoneme sequence/empty phoneme/duplicate/行号准确性/未命中返回空/move；MappingS2P 9 cases：空流/合法 TSV/CRLF/missing tab/multiple tabs/empty original/empty target/duplicate/未映射透传/空输入/空映射=DirectS2P/move；LanguageResource 4 cases：direct 无 onset 可构造/direct 缺失文件抛 std::runtime_error/dictionary 缺失文件抛异常/move-only static_assert；Lite 真实场景 3 cases：dict-mode TSV 解析/direct-mode 切分/损坏字典显式报错）。**Driver**：`test_onnx_driver_config.cpp`（OnnxDriverConfig 默认值 DML+0/可定制/ExecutionProvider 枚举稳定性 CPU=0/CUDA=1/DML=2/CoreML=3/DriverInitArgs 默认 ep=CPU+deviceIndex=-1+runtimePath 空/API_NAME+API_VERSION 命名空间作用域常量+实例 objectName()/version 绑定/SessionOpenArgs 默认 useCpu=false+ep/deviceIndex nullopt/可覆盖 EP/OnnxDriverConfig→DriverInitArgs EP 赋值兼容/setupOnnxInferenceDriver 函数签名存在性取地址/no-auto-fallback 契约文档化/Lite SynthrtEngine EP 选择 DML/CUDA/CPU 三场景）；`CMakeLists.txt` 添加 GLOB `test_*.cpp` + 新 target `synthrt-unittest-driver-ext` 链接 `synthrt::driver`。**C ABI**：`test_c_abi_input_validation.cpp`（CODING-02 extern "C" 边界 try-catch：srt_session_set_package_paths null session/null paths+count>0/null paths+count=0/空列表/有效路径；srt_session_destroy null 幂等；vnext handles create_v2/destroy_v2/destroy_v2 null 幂等/已销毁返回 INVALID_HANDLE；srt_session_set_roots/set_reserved_phonemes 输入校验；srt_session_create_with_resources null runtime/langService/both null/invalid handles；srt_runtime_destroy/srt_language_service_destroy null 幂等/已销毁返回 INVALID_HANDLE；srt_session_snapshot null handle/fresh 返回 null；srt_session_refresh_async null；srt_model_create null session/packageId/singerId/version；srt_model_destroy/srt_task_destroy/srt_free_buffer null 幂等；srt_task_state/wait/cancel/result_json null；双通道错误消息 srt_last_error()+srt_last_error_code()；extern "C" 边界异常隔离；Lite SynthrtEngine 初始化序列）。设计原则对齐：ROBUST-01 (Expected 传播)、ROBUST-05 (显式报错+行号上下文)、CODING-02 (extern "C" 异常隔离)、INFRA-04 (Catch2 v3)、CODING-03 (路径 to_utf8)。源码观察（非 bug，不修复）：`LanguageResource::direct/dictionary` 返回值非 `Expected<LanguageResource>`，文件不存在时抛 `std::runtime_error`，与 ROBUST-01 偏离，已在测试中通过 `REQUIRE_THROWS_AS` 记录实际行为。Driver 测试修复一处作用域错误：`API_NAME`/`API_VERSION` 是 `srt::driver::onnx` 命名空间作用域 `inline constexpr` 常量（非 DriverInitArgs 静态成员），改为 `srt::driver::onnx::API_NAME` 引用并辅以实例 `objectName()`/`version` 验证构造函数绑定。所有文件 GetDiagnostics 无错误。 |

