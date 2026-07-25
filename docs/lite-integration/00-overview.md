# Lite 对接重构方案 — 总览

日期: 2026-07-26（v7 修订：auto-init 触发条件勘误 + 测试文件声明勘误 + CMake 描述勘误；v6 修订：补全 Phase A/B 完成状态 + 错误码勘误记录；v3 修订：以 lite 为主体，核对实际代码）
状态: ☑ Phase A/B 全部完成（synthrt: 37e5b3d/d0015af/4e0a3ff；lite: dcb2b61a/c323aee2/f652052f/d8e8626f/0a88862d）；☐ C1/C2 待用户更新 vcpkg 后回归；**v7 勘误：A1/A2 单元测试文件实际未创建（待 C1 阶段补齐）**
范围: `d:\projects\synthrt` + `d:\projects\ds-editor-lite`

---

## 1. 目标与原则

**以 lite 为主体**重构对接方案：在合理范围内**优先改动本项目（synthrt）接口**以适配 lite 的简洁调用需求；**抛弃历史包袱**，删除 lite 中不必要的兼容与映射层；不留转发桥接。

### 1.1 核心原则

1. **lite 主体性**：synthrt 在 Level=2 允许的范围内大胆追加 lite 真正需要的便捷 API；不要求 lite 适配 synthrt 的旧式组件式调用
2. **抛弃历史包袱**：删除 lite 中 `PackageCatalog` / `SingerModelSession` / `m_loadedPackageDirs` / `m_singerSessions` / `G2pOnnxSessionTask` / `G2pOnnxSessionFactory` 等中间层；新增 `SingerIdentifier::operator SingerRef()` 隐式转换（v2 核对：lite 现有代码**不存在** `toSingerRef`/`toSingerIdentifier` helper，无需删除）
3. **不留转发桥接**：旧公共 API（`refreshVoicebanks` / `singerSnapshot` / `findSinger` / `packageDirectory` / `acquireSingerSession` / `resolveLanguageRoute` / `resolveS2pResource`）**全部删除**，调用方一次性迁移到 `VoicebankSession` API；不为旧 API 保留转发实现

> **v3 核对修正**：
> - `VoicebankSession::refresh()` **无参数** — 路径通过 `setRoots()` 设置（[VoicebankSession.h#L197-L214](file:///d:/projects/synthrt/include/diffsinger/Session/VoicebankSession.h)）。v2 文档 `m_session.refresh(searchPaths)` 错误，已修正为 `setRoots(paths)` + `refresh()`
> - `SessionResources.languageService` 非空时，`refresh()` 内部**自动调用** `LanguageService::initializeMetadata()`（首次）/`updateMetadata()`（增量），将 `g2pPluginPaths` + `officialG2pPackages` 作为参数传入（[VoicebankSession.cpp#L583-L620](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp) — 触发条件为 `if (svc)` 即 `languageService != nullptr`，非 `g2pPluginPaths` 非空）。v2 独立 B2 阶段"手动初始化 LanguageService"基本不必要，已合并到 B1
> - 实际外部调用方约 **10 个文件**（非 v2 所称 23 个）：6 个直接调用 SynthrtEngine 旧 API（InferEngine/G2pService/GetPronunciationTask/GetPhonemeNameTask/PackageManager）+ 4 个经 `InferEngine::acquireSingerSession` 间接调用（Acoustic/Duration/Pitch/Variance task；**Vocoder 不调用**）
> - `SynthrtEngine::findSinger(singerId)` / `singerSnapshot` / `packageDirectory` **无外部调用方**（仅 SynthrtEngine.cpp 内部使用）；UI 层的 `findSinger` 调用均为 `packageManager->findSingerByIdentifier(...)`（PackageManager 自有方法，非 SynthrtEngine）
> - lite `m_langSvc` 现为值类型 `srt::g2p::LanguageService m_langSvc;`；`SessionResources` 要求 `std::shared_ptr<srt::g2p::LanguageService>`，需改为 `shared_ptr` 并在构造函数 `make_shared`
4. **谨慎改动 synthrt 公共契约**：ARCH-02 Level=2 冻结 — 仅追加新 API，不修改现有签名/字段语义；不删除已 `[[deprecated]]` 标记的接口（由 Level=3 统一清理）；不动公共头 `_` 前缀成员（CS-03 冻结）
5. **执行粒度**：按 user_rules "完成单个任务后单独提交但不推送"，分阶段提交，每阶段独立可编译可测试

### 1.2 与 v1/v2 方案的差异

| 维度 | v1（已废弃） | v2（已废弃） | v3（本文） |
|---|---|---|---|
| synthrt 侧改动 | 仅 A1 | A1 + A2 | A1 + A2（不变） |
| `refresh()` 调用 | — | `m_session.refresh(searchPaths)` ❌ | `setRoots(paths)` + `refresh()` ✓ |
| LanguageService 初始化 | — | 独立 B2 手动 `initializeMetadata` | SessionResources 自动初始化，B2 合并到 B1 |
| InferEngine 迁移 | — | 4 个 task 逐一迁移 | 只迁移 InferEngine + ActiveInference 适配；4 task 仅类型替换 |
| 调用方数量 | — | 23（夸大） | 10（核对实际） |
| 阶段数 | 5（B1-B5） | 4（B1-B4） | 3（B1 子阶段 B1a/B1b/B1c + B2 + B3） |
| 兼容层 | B3 桥接 | 无桥接 | 无桥接 |
| `toSingerRef` helper | — | "删除"（实际不存在） | 不涉及（v2 误报） |

---

## 2. 文档分类

| 文件 | 内容 |
|---|---|
| `00-overview.md` | 本文：目标、原则、范围与执行顺序 |
| `01-current-state-analysis.md` | 现状分析与痛点清单 |
| `02-synthrt-side-changes.md` | 本项目侧改动（A1 + A2 + A3） |
| `03-lite-side-migration.md` | lite 侧 4 阶段迁移方案（无桥接） |
| `04-interface-contract.md` | lite ↔ synthrt 接口契约 |
| `05-verification-checklist.md` | 验证清单与回归项 |

---

## 3. 设计准则核对

依据 [docs/design/design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) 逐项核对：

### ARCH-02 (Level 锚定兼容性) — ☑ 已落地
- 本项目侧改动**仅追加**：A1（commit 37e5b3d）新增 `setupG2pOnnxDriver` 函数、A2（commit d0015af）在 `VoicebankSnapshot` 添加 const 成员方法、A3（commit 4e0a3ff）文档更新。不修改任何现有签名。
- 已 `[[deprecated]]` 标记的接口（`LanguageService::initialize(map)`、3-arg `resolveLanguageRoute`/`resolveS2pResource`/`convert`、`VoicebankSession::setRuntime`/`setLanguageService`）保留至 Level=3 清理；本轮仅迁移 lite 调用方，不删除接口。

### ARCH-03 (组合优于继承和封装转发)
- "宿主层 `SynthrtEngine` 直接组合 `VoicebankScanner`、`LanguageService`、`Runtime`、`SingerStageResolver`、`ModelSet`。synthrt 不新增 facade 或转发层。"
- `ds::session::VoicebankSession` 是 synthrt 既有的、文档化的会话入口（[docs/modules/overview.md](file:///d:/projects/synthrt/docs/modules/overview.md) §3.1 "vnext 推荐，宿主层唯一入口"），非新增 facade。lite 采用它符合本规则。
- 本项目侧**不**新增包装 `VoicebankSession` 的转发层；A1/A2 是低层 setup/查询 helper（与既有 `setupOnnxInferenceDriver` 平级，const inline 查询方法），不构成 facade。

### ARCH-04 (直接句柄而非映射层) — ☑ 已落地
- lite 当前的 `SingerModelSession` 是 `SingerIdentifier → ModelSet` 的映射层，**违反本规则**。（已在 B1c commit f652052f 删除文件）
- `SynthrtEngine::toSingerRef` / `toSingerIdentifier` 显式 helper 是不必要的类型转换映射层。（v3 核对：lite 不存在此 helper；v6：B1a 已添加隐式 `operator SingerRef()` commit dcb2b61a）
- 迁移后 lite 直接持有 `ModelSetHandle`（synthrt 直接句柄）；`SingerIdentifier` 提供隐式 `operator SingerRef()` 实现类型边界透明转换，无显式 helper 函数。

### ARCH-05 (最小但完整的模型生命周期) — ☑ 已落地
- `ModelSetHandle::load/start/stop/reset/unload/unloadAll/isLoaded/isStale/stages` 覆盖完整生命周期。lite 不再在 `SingerModelSession` 中重复封装。（已在 B1c 删除）

### INFRA-02 (不保留无期限兼容层) — ☑ 已落地
- lite 是内部代码无外部契约：`PackageCatalog` / `SingerModelSession` / `m_loadedPackageDirs` / `m_singerSessions` / G2P ONNX adapter / `SynthrtEngine` 旧公共 API **完全删除**（B1c commit f652052f），不保留转发兼容层。
- synthrt deprecated 接口由 Level=3 统一清理，本轮不强制。

### ROBUST-05 (出错必须显式报错) — ☑ 已落地
- lite 当前的 `PackageScanAfterInitialize` 是显式错误，但**语义错误**（应为 stale 机制而非硬拒绝）。（已在 B1c 删除）
- 迁移后由 `ErrorCode::StaleModelSet` (216) 表达，宿主重试一次（D-30）。

### CODING-01 (语言与格式) — ☑ 已落地
- 新增 API 遵循命名规范：`setupG2pOnnxDriver`（snake_case 函数）、`VoicebankSnapshot::findSinger`（lower-camel 方法）。
- `SingerIdentifier::operator SingerRef()` 为 `explicit`？**不**，因字段一一对应且语义等价，允许隐式转换以简化调用方代码（与 `QString::toStdString` 隐式语义一致）。

---

## 4. 范围与执行顺序

### 范围

| 侧 | 内容 | 风险 | 状态 |
|---|---|---|---|
| synthrt | A1 追加 `setupG2pOnnxDriver` helper；A2 在 `VoicebankSnapshot` 追加 4 个 const 查询方法；A3 文档更新 | 低 — 纯追加 | ☑ 已完成（37e5b3d/d0015af/4e0a3ff） |
| lite | `SynthrtEngine` 重写为 `Runtime + LanguageService(shared_ptr) + VoicebankSession`；删除 `PackageCatalog`/`SingerModelSession`/adapter/旧 API；`SingerIdentifier` 添加隐式 `operator SingerRef()`；**10 个文件**迁移到 `VoicebankSession` API（InferEngine 单点 chokepoint + 5 个直接调用方 + 4 个 task 类型替换） | 中 — 影响推理主链路 | ☑ 已完成（dcb2b61a/c323aee2/f652052f/d8e8626f） |

### 执行顺序

```
Phase A (synthrt) — ☑ 已完成
  A1 追加 setupG2pOnnxDriver helper              [02 §A1]  ☑ 37e5b3d
  A2 VoicebankSnapshot::findSinger/findSingersBySingerId/findPackage/findManifest  ☑ d0015af
  A3 文档更新                                    [02 §A3]  ☑ 4e0a3ff
       │
       ▼ (依赖 A 完成)
Phase B (lite) — ☑ 已完成
  B1a SingerIdentifier::operator SingerRef()                                       ☑ dcb2b61a
     + SynthrtEngine 添加 m_session 成员 + session() API（双 API 共存，旧 API 保留）
     + m_langSvc 改为 shared_ptr + 填充 SessionResources
       │
       ▼
  B1b 逐文件迁移调用方（每步独立可编译）                                            ☑ c323aee2
     1) InferEngine::acquireSingerSession → 返回 shared_ptr<ModelSetHandle>
        + ActiveInference 适配 ModelSetHandle（4 个 task 仅类型替换）
     2) PackageManager::refreshInstalledPackages → session().setRoots()+refresh()+snapshot 查询
     3) G2pService / GetPronunciationTask → session().convertG2p
        GetPhonemeNameTask → session().convertS2p（B1c 补完成）
       │
       ▼
  B1c 删除旧 SynthrtEngine API + PackageCatalog/SingerModelSession 文件 + adapter 类  ☑ f652052f
       + GetPhonemeNameTask 补迁移（B1b-3 文档错误）
       │
       ▼
  B2 (合并自 v2 B2) LanguageService 自动初始化由 SessionResources 触发              ☑ 已合并
     + version-aware 路由由 VoicebankSession 内部处理（lite 无需手动 initializeMetadata）
       │
       ▼
  B3 findSinger 多版本歧义改用 SvsSingerAmbiguous（PackageManager 新增 helper）     ☑ d8e8626f
       │
       ▼
Phase C (验证) — ☐ 待用户更新 vcpkg 后执行
  C1 单元 + 集成测试回归                         [05]
  C2 lite 端到端推理冒烟                         [05]
```

**v6 文档勘误（vs v3/v4/v5）**：
- 错误码数值勘误：v3 文档中 `InferenceNotInitialized=211` / `InferenceOutputEmpty=213` / `InferenceDataTypeMismatch=214` / `SvsSingerNotFound=605` 4 处数值错误，v6 已对照 [Diagnostic.h#L66-L137](file:///d:/projects/synthrt/include/synthrt/Core/Support/Diagnostic.h#L66-L137) 实测修正为 `200 / 212 / 213 / 600`。lite `PackageManager.cpp:352` 注释中 `(605)` 也已同步勘误为 `(600)`（commit 0a88862d）
- 03.md B3.1 代码示例勘误：v5 标称"实际实现"的代码块与 lite 真实代码不符；v6 已逐行核对修正（详见 [03-lite-side-migration.md](file:///d:/projects/synthrt/docs/lite-integration/03-lite-side-migration.md) §B3.1）
- 03.md B1a-T02 typo 修正：原 `session.snapshot()` 缺少调用括号；v6 修正为 `session().snapshot()`

**v7 文档勘误（vs v6）**：
- **auto-init 触发条件勘误**（CRITICAL）：v6 文档多处描述 "当 `SessionResources.g2pPluginPaths` 非空时 `refresh()` 内部自动调用 `initializeMetadata/updateMetadata`"，实际代码 [VoicebankSession.cpp#L583](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp#L583) 触发条件为 `if (svc)` 即 `languageService != nullptr`；`g2pPluginPaths` 仅作为参数传入，**不**作为触发条件。v7 已同步修正 00/02/03/04-lite + `docs/modules/g2p.md` + `docs/modules/ds-session.md` 共 8 处描述
- **单元测试文件声明勘误**（CRITICAL）：v6 文档声称 A1 commit 37e5b3d 含 `unittests/G2P/tst_g2p_onnx_setup.cpp`、A2 commit d0015af 含 `domains/ds-session/unittests/tst_vbs_snapshot_query.cpp`，实际两个测试文件**均未创建**（CMakeLists.txt 也未引用）。v7 已修正 02/05 文档状态为"☐ 测试文件未创建"，待 C1 阶段补齐
- **CMake 描述勘误**：v6 文档 §A1 实现要点 4 称 "增加新源文件 `G2pOnnxSetup.cpp`，无需新依赖"，实际 `lib/G2P/CMakeLists.txt` 使用 `file(GLOB_RECURSE)` 自动发现源文件（无需显式添加），且**新增 `srt::driver` 依赖**（注释明确说明）。v7 已修正
- **SessionResources 字段勘误**：v6 `docs/modules/ds-session.md` 的 `SessionResources` 定义仅含 `runtime` + `languageService` 2 字段，实际 header 含 4 字段（追加 `g2pPluginPaths` + `officialG2pPackages`）。v7 已补全

**v3 关键修正（历史保留）**：
- `refresh()` 无参数，路径经 `setRoots()` 设置（v2 错误）
- B2 合并到 B1：`SessionResources.languageService` 非空时 `refresh()` 自动 `initializeMetadata/updateMetadata`，传入 `g2pPluginPaths` 作为参数（v7 勘误：触发条件是 `languageService` 非空，非 `g2pPluginPaths` 非空），lite 无需手动初始化 LanguageService
- B1 拆为 3 子阶段（B1a/B1b/B1c）：双 API 共存 → 逐文件迁移 → 删除旧 API；每子阶段独立可编译
- InferEngine 是单点 chokepoint：迁移 `acquireSingerSession` + 适配 `ActiveInference`，4 个 task 仅类型替换
- 实际调用方 10 个文件（非 23）：`acquireSingerSession` 1 + `resolveLanguageRoute` 3 + `resolveS2pResource` 1 + `refreshVoicebanks` 1 + 4 task 间接

### 不在本方案范围

- 新增声库热重载完整卸载（removed 包，需 Phase 3）— 见 [project_memory](file:///c:/Users/99662/.trae-cn/memory/projects/-d-projects-synthrt/project_memory.md) "Hot reload must handle新增/替换包 but not fully unload"
- C ABI `srt_session_*` 句柄 API 的 lite 直连方案 — lite 走 C++ 接口
- 推理缓存 LRU 回收策略（K-09 之外）
- dsinfer-cli 调用模式调整 — CLI 已使用 `VoicebankSession` 风格
- 5 个 DiffSinger 插件 BF-61 错误路径覆盖 — 独立任务
- 升级到 Level=3 删除 deprecated 接口 — 独立任务

---

## 5. 决策记录

| 决策 | 选择 | 理由 |
|---|---|---|
| lite 是否保留 PackageCatalog 作为 VoicebankSession 的 Qt 缓存层 | **否** | VoicebankSnapshot 已是不可变共享快照；PackageCatalog 重复 generation + fingerprint 逻辑；属不必要映射层 |
| lite 是否保留 PackageManager 模块 | **是** | PackageManager 是 UI 模型转换层（synthrt snapshot → Qt PackageInfo/SingerInfo），非映射层；保留但内部改用 `session().refresh()` + `snapshot->findPackage/findSinger` |
| lite 是否新增 "SynthrtHostSession" 包装 VoicebankSession | **否** | 违反 ARCH-03；VoicebankSession 已是文档化入口 |
| synthrt 是否新增 G2P ONNX driver setup helper | **是（A1）** | 与既有 `setupOnnxInferenceDriver` 平级，消除 lite 110 行通用样板代码；属基础设施而非 facade |
| synthrt 是否在 VoicebankSnapshot 添加 findSinger/findPackage/findManifest | **是（A2）** | lite 频繁按 SingerRef 反查 singer/package/manifest，遍历 O(n) 在热路径重复；const 方法不破坏 ABI；与 `std::vector::find` 语义一致 |
| SingerIdentifier ↔ SingerRef 转换方式 | **隐式 `operator SingerRef()`** | 字段一一对应；调用方 `session.ensureModelSet(identifier)` 一行可写 |
| LanguageService 初始化方式 | **SessionResources 自动初始化** | `languageService` 非空时 `refresh()` 内部调用 `initializeMetadata/updateMetadata`（VoicebankSession.cpp:583-620，触发条件 `if (svc)`）；lite 无需手动初始化，B2 合并到 B1 |
| `m_langSvc` 类型 | **`std::shared_ptr<srt::g2p::LanguageService>`** | `SessionResources` 要求 `shared_ptr`；当前 lite 为值类型，需改 |
| InferEngine 迁移策略 | **只迁移 InferEngine + ActiveInference 适配** | 4 个 task 经 `InferEngine::acquireSingerSession` 间接调用；ActiveInference 包装器适配 `ModelSetHandle` 后，task 仅类型替换 |
| 旧 SynthrtEngine 公共 API（refreshVoicebanks 等）处理 | **全部删除** | 违反"不留转发桥接"原则；调用方一次性迁移到 `VoicebankSession` API |
| lite `findSinger` 多版本歧义错误码 | `SvsSingerAmbiguous` (604) | 与 D-41/D-42 镜像；VoicebankSession 内部已实现，lite 仅移除自实现 |
| 迁移完成后是否保留 `setRuntime`/`setLanguageService` deprecated 接口 | **保留** | Level=2 冻结；由 Level=3 统一清理 |
| B1 提交粒度 | **3 子阶段（B1a/B1b/B1c）** | B1a 双 API 共存可编译；B1b 逐文件迁移每步可编译；B1c 删除旧 API；缓解单 commit 过大风险 |
