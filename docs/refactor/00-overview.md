# SynthRT 代码规范化与缺陷修复 — 总览

日期: 2026-07-26（v1 初稿，结合 `D:\projects\ds-editor-lite` 真实需求）
范围: `d:\projects\synthrt`（仅本项目；lite 侧改动不在范围）
状态: ☐ 待执行（任务跟踪表见 §4）

---

## 1. 定位与边界

本文档系**本轮**结合 ds-editor-lite 真实使用场景对 synthrt 代码进行的规范化、隐藏 bug 修复与缺失 API 谨慎补充的执行方案。**不重复** [docs/lite-integration/](file:///d:/projects/synthrt/docs/lite-integration/) 已落地的对接契约与迁移计划；本文聚焦 lite 已迁移至 `VoicebankSession` API 之后，在持续迭代中累积的代码规范偏差、文档不一致、潜在运行时缺陷与 API 表面缺口。

### 1.1 与既有文档的关系

| 既有文档 | 角色 | 本文关系 |
|---|---|---|
| [docs/design/design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) | 设计准则（ARCH/ROBUST/INFRA/PACK/CODING） | 本文任务全部对照该准则核对合理性 |
| [docs/lite-integration/](file:///d:/projects/synthrt/docs/lite-integration/) | lite 对接契约与迁移方案（v8 已完成 A1/A2/A3 + B1a/B1b/B1c/B3） | 本文不修改其 API 契约；如发现契约偏差，提 Bug 而非改契约 |
| [docs/modules/](file:///d:/projects/synthrt/docs/modules/) | 模块 API 文档 | 本文修正其中的笔误与失效引用 |

### 1.2 不在范围

- Level=3 deprecated 接口清理（`setRuntime`/`setLanguageService`/`LanguageService::initialize(map)`/3-arg `resolveLanguageRoute` 等）
- lite 侧代码改动（`D:\projects\ds-editor-lite` 不在本文档范围）
- 新声库热重载完整卸载、C ABI lite 直连方案、推理缓存 LRU 回收策略（独立任务，见 [00-overview.md](file:///d:/projects/synthrt/docs/lite-integration/00-overview.md) §4 "不在本方案范围"）

---

## 2. 探索结论摘要

经 [01-current-state-analysis.md](file:///d:/projects/synthrt/docs/lite-integration/01-current-state-analysis.md) §3 表格核对与本次对 lite 实际调用方（`SynthrtEngine.cpp` / `InferEngine.cpp` / `PackageManager.cpp` / `G2pService.cpp` / `GetPronunciationTask.cpp` / `ExtractorUtils.cpp` / 4 个 InferTask）的源码审视：

1. **API 表面完整**：synthrt 已提供 lite 全部 voicebank / G2P / S2P / 推理 / 音频 / 抽取所需公共 API，无关键缺口。详见 [03-missing-apis.md](file:///d:/projects/synthrt/docs/refactor/03-missing-apis.md)。
2. **代码规范化偏差集中在 3 类**：`ds::lang::` 失效 namespace 引用、`.string()` 路径处理违反 CODING-03、仓库根目录遗留 squash 工件。详见 [01-code-normalization.md](file:///d:/projects/synthrt/docs/refactor/01-code-normalization.md)。
3. **隐藏 bug 已识别 4 项，全部已下结论**：B1（docs 笔误，与 N1 合并修复）、B2（已恢复）、B3（catch(...) 已核实无 bug）、B4（convertG2p 异常边界不一致，已核实并修复 commit 2760ffb）。详见 [02-bugfixes.md](file:///d:/projects/synthrt/docs/refactor/02-bugfixes.md)。
4. **测试缺口**：A1/A2 单元测试已通过（v8 状态），C2 端到端冒烟仍待用户更新 vcpkg。详见 [04-test-coverage.md](file:///d:/projects/synthrt/docs/refactor/04-test-coverage.md)。

---

## 3. 核心原则（依据 user_rules）

1. **谨慎的规范化**：仅修复违反 [design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) CODING-01..06 的明确偏差；不做"风格偏好"型改动；不动公共头文件 `_` 前缀成员（CS-03 冻结）。
2. **缺失 API 反复核实后补充**：任何"缺失 API"必须经 lite 真实调用方核对，确属 synthrt 表面缺口而非 lite 调用方式问题，才谨慎追加。详见 [03-missing-apis.md](file:///d:/projects/synthrt/docs/refactor/03-missing-apis.md) §1 核实流程。
3. **逐个执行、单独提交、不推送**：每个任务一次 `git commit`，commit message 形如 `refactor(N1): drop stale ds::lang references in module docs`。完成单个任务后立即更新本文 §4 任务跟踪表的状态列。
4. **设计准则核对**：每条任务执行前对照 ARCH/ROBUST/CODING 检验合理性；如有偏差，先调整方案再执行。

---

## 4. 任务跟踪表

> 状态标记：☐ 待执行 / ⏳ 进行中 / ☑ 已完成 / ✗ 已驳回（见"驳回理由"列）

### 4.1 代码规范化（[01](file:///d:/projects/synthrt/docs/refactor/01-code-normalization.md)）

| ID | 任务 | 优先级 | 状态 | 提交 hash | 驳回理由 |
|---|---|---|---|---|---|
| N1 | 修正 4 个 docs 文件中失效的 `ds::lang::` 引用（含 design-guidelines.md） | 中 | ☑ | d81895c | — |
| N2 | 修正生产代码 `.string()` 路径处理违反 CODING-03 的 2 处（GameExtractor.cpp） | 中 | ☑ | 4136a15 | — |
| N3 | 处理仓库根 `_commits_to_squash.txt` + `_squash.ps1` 遗留工件 | 低 | ☑ | — | 删除 untracked 临时工件（squash 已完成，4 phase commits 已提交）；无需 git commit |

### 4.2 Bug 修复（[02](file:///d:/projects/synthrt/docs/refactor/02-bugfixes.md)）

| ID | 任务 | 优先级 | 状态 | 提交 hash | 驳回理由 |
|---|---|---|---|---|---|
| B1 | 修正 module docs 中 `ds::lang::` 失效引用（与 N1 同因，合并提交） | 中 | ☑ | d81895c | — |
| B2 | docs/lite-integration/ 已恢复（git restore 完成，无需提交） | 高 | ☑ | — | — |
| B3 | 核实 VoicebankSession.cpp:893 `catch(...)` 是否设置错误 | 低 | ☑ | — | 已核实：catch(...) 正确返回 S2pConversionFailed Error，符合 CODING-02 |
| B4 | 核实 `convertG2p` 缺少 try/catch 异常边界（与 `convertS2p` 不一致） | 中 | ☑ | 2760ffb | 已核实 + 已修复：Task::start() 非 noexcept，调用链无 try/catch；加 try/catch 返回 G2pConversionFailed |

### 4.3 缺失 API 补充（[03](file:///d:/projects/synthrt/docs/refactor/03-missing-apis.md)）

| ID | 任务 | 优先级 | 状态 | 提交 hash | 驳回理由 |
|---|---|---|---|---|---|
| M1 | 核实 lite `ActiveInference` 是否绕过 `ModelSetHandle::start()` staleness 检查 | 中 | ☑ | — | 已核实：lite 确实绕过，但是设计意图（"already-loaded models may finish on stale set"），非 synthrt 缺口 |
| M2 | （条件性）若 M1 确认存在 staleness 绕过，谨慎追加 helper API | 中 | ✗ | — | M1 不成立 → M2 不执行；现有 API 已足够，追加 helper 违反 ARCH-02 精神 |

### 4.4 测试覆盖（[04](file:///d:/projects/synthrt/docs/refactor/04-test-coverage.md)）

| ID | 任务 | 优先级 | 状态 | 提交 hash | 驳回理由 |
|---|---|---|---|---|---|
| T1 | 等待用户更新 vcpkg 后执行 C2 端到端冒烟 | 高 | ☐（阻塞） | — | — |
| T2 | 评估 GameExtractor.cpp CODING-03 修复是否需补路径规范化单测 | 低 | ☑ | — | 已评估：不补单测（错误消息编码非功能行为，成本高于收益） |
| T3 | B4 修复（若执行）需补异常边界单测 | 低 | ☐（条件性） | — | 待 B4 核实后解锁 |

---

## 5. 文档分类

| 文件 | 内容 |
|---|---|
| `00-overview.md` | 本文：定位、原则、范围、任务跟踪表 |
| `01-code-normalization.md` | CODING-01..06 违反项与修复方案 |
| `02-bugfixes.md` | 隐藏 bug 清单与核实记录 |
| `03-missing-apis.md` | lite 所需 API 核实流程与结论 |
| `04-test-coverage.md` | 测试覆盖缺口与新增测试计划 |

---

## 6. 修订记录

| 日期 | 版本 | 说明 |
|---|---|---|
| 2026-07-26 | v1 | 初稿：基于 lite 真实调用方核对，识别 3 类规范化偏差 + 2 项 bug + 1 项 API 待核实 |
| 2026-07-26 | v2 | N1/N2/N3/B1/B2/B3/B4/M1/M2 全部已下结论；N1(d81895c)/N2(4136a15)/B1(d81895c)/B4(2760ffb) 已提交；M1/M2 不追加 API；T1 阻塞、T2 待评估、T3 待补 |
