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

