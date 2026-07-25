# 缺失 API 核实流程与结论

日期: 2026-07-26（v1 初稿）
范围: 仅本项目（synthrt）公共 API 表面；lite 侧调用方式不在范围
原则: 任何"缺失 API"必须经 lite 真实调用方核对，确属 synthrt 表面缺口而非 lite 调用方式问题，才谨慎追加

---

## 1. 核实流程

每条"缺失 API"候选项按以下步骤核实：

1. **定位调用点**：在 lite 源码中 grep 该 API 的预期调用方，确认 lite 确实需要该 API
2. **核查 synthrt 现有 API**：在 synthrt `include/` 与 `domains/` 中 grep 是否已有等价 API
3. **判断归属**：
   - 若 synthrt 已有等价 API → 非 synthrt 缺口，lite 侧改用现有 API（不追加）
   - 若 synthrt 无等价 API，但 lite 可通过现有 API 组合实现 → 非 synthrt 缺口（不追加）
   - 若 synthrt 无等价 API，且 lite 必须自实现中间层 → synthrt 缺口（谨慎追加）
4. **设计准则核对**：追加 API 需符合 [design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) ARCH-02 Level=2（仅追加不修改签名）+ ARCH-03（不新增 facade）
5. **结论**：明确为"追加 / 不追加 / 待核实"，并给出理由

---

## 2. API 候选项清单

### M1: 核实 lite `ActiveInference` 是否绕过 `ModelSetHandle::start()` staleness 检查 — ☑ 已核实

**结论**：**非 synthrt 缺口，不追加 API**。lite 确实绕过 `ModelSetHandle::start()`，但这是 lite 侧的设计选择，符合 synthrt 的生命周期契约。

**核实记录**：

#### 2.1.1 synthrt 侧 `ModelSetHandle` 生命周期契约

[ModelSetHandle.h#L16-L27](file:///d:/projects/synthrt/include/diffsinger/Session/ModelSetHandle.h) 明确：

> - `start()` 是**唯一**拒绝 stale handle 的操作（返回 `StaleModelSet`）
> - `load/stop/unload` 不拒绝 stale handle
> - "already-loaded models and already-running tasks are not aborted"（vnext 03 lifecycle contract）

[ModelSetHandle.cpp#L92-L97](file:///d:/projects/synthrt/domains/ds-session/lib/ModelSetHandle.cpp) `load()` 注释：

> "load() intentionally does NOT reject stale handles: a caller may inspect or finish work on an already-loaded model after the snapshot has moved on. Only start() rejects staleness."

#### 2.1.2 lite 侧调用链

[InferTaskCommon.cpp#L47-L81](file:///d:/projects/ds-editor-lite/src/app/Modules/Inference/Tasks/InferTaskCommon.cpp) `ActiveInference::acquire`：

```cpp
auto loadExp = handle->load(kind);   // 不检查 staleness
if (!loadExp) return loadExp.takeError();
Model model;
model.inference = *loadExp;
// ... 不调用 handle->start() ...
return Handle(*this, std::move(model), generation);
```

4 个 InferTask 直接调用 `inference->start(input)`（`srt::svs::Inference::start()`），**绕过** `ModelSetHandle::start()`：

| 文件 | 行号 | 调用 |
|---|---|---|
| [InferDurationTask.cpp#L205](file:///d:/projects/ds-editor-lite/src/app/Modules/Inference/Tasks/InferDurationTask.cpp) | 205 | `inferenceDuration->start(input)` |
| [InferPitchTask.cpp#L192](file:///d:/projects/ds-editor-lite/src/app/Modules/Inference/Tasks/InferPitchTask.cpp) | 192 | `inferencePitch->start(input)` |
| [InferVarianceTask.cpp#L199](file:///d:/projects/ds-editor-lite/src/app/Modules/Inference/Tasks/InferVarianceTask.cpp) | 199 | `inferenceVariance->start(input)` |
| [InferAcousticTask.cpp#L206](file:///d:/projects/ds-editor-lite/src/app/Modules/Inference/Tasks/InferAcousticTask.cpp) | 206 | `inferenceAcoustic->start(input)` |

#### 2.1.3 staleness 检查实际发生位置

[InferEngine.cpp#L205-L238](file:///d:/projects/ds-editor-lite/src/app/Modules/Inference/InferEngine.cpp) `acquireSingerSession`：

```cpp
auto exp = session.ensureModelSet(identifier);   // 创建 fresh handle
if (!exp) {
    if (exp.error().code() != srt::core::ErrorCode::StaleModelSet) { ... }
    // StaleModelSet — 重试一次
    exp = session.ensureModelSet(identifier);
    ...
}
return *exp;
```

#### 2.1.4 关键发现

[VoicebankSession.cpp#L962-L1100](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp) `createModelSet` / `ensureModelSet`：

- `ensureModelSet` 是 `createModelSet` 的薄包装（line 1096-1101）
- `createModelSet` **不会返回 `StaleModelSet`**：每次调用都创建新 handle，绑定当前 snapshot generation
- `StaleModelSet` 只在 `ModelSetHandle::start()` 和 `ModelSet::start()` 中返回

因此 lite 的 `StaleModelSet` 重试逻辑（line 220-236）**当前不会触发**（`ensureModelSet` 永远不返回 `StaleModelSet`）。这是 lite 侧的防御性代码，非 synthrt 缺口。

#### 2.1.5 结论

| 判定项 | 结论 |
|---|---|
| lite 是否绕过 `ModelSetHandle::start()`？ | ☑ 是 |
| 这是否违反 synthrt 生命周期契约？ | ✗ 否（"already-loaded models may finish on stale set" 是设计意图） |
| synthrt 是否需要追加 helper API？ | ✗ 否（lite 的 staleness 检查在 acquire 阶段通过 `ensureModelSet` 创建 fresh handle 完成；task 执行阶段允许 stale handle 继续运行） |
| lite 侧的 `StaleModelSet` 重试逻辑是否有效？ | ⚠ 当前死代码（`createModelSet` 不返回 `StaleModelSet`），但不影响正确性（lite 侧问题，非本文档范围） |

**状态**：☑ 已核实，不追加 API。

---

### M2: （条件性）若 M1 确认存在 staleness 绕过，谨慎追加 helper API — ✗ 已驳回

**结论**：**已驳回**。M1 确认非 synthrt 缺口，M2 不执行。

**驳回理由**：
- synthrt `ModelSetHandle` 生命周期契约明确允许 stale handle 上的已加载模型继续运行
- lite 的 staleness 检查在 acquire 阶段（`ensureModelSet` 创建 fresh handle）已完成
- 追加 helper API（如 `ModelSetHandle::tryStart()` 或 `VoicebankSession::acquireFreshHandle()`）会违反 ARCH-02 Level=2（仅追加不修改签名）的精神——现有 API 已足够，无需新 facade

**状态**：✗ 已驳回（M1 不成立 → M2 不执行）

---

## 3. 其他 API 候选项（已扫描，无缺口）

经 [01-current-state-analysis.md](file:///d:/projects/synthrt/docs/lite-integration/01-current-state-analysis.md) §3 表格核对，synthrt 公共 API 已覆盖 lite 全部需求：

| lite 调用域 | synthrt API | 覆盖状态 |
|---|---|---|
| voicebank 扫描 | `VoicebankScanner::scan()` / `VoicebankSession::setRoots()` / `refresh()` | ☑ 完整 |
| 声库查询 | `VoicebankSnapshot::findSinger/findPackage/findManifest`（A2 已新增） | ☑ 完整 |
| G2P 转换 | `VoicebankSession::convertG2p()` / `ensureLanguageReady()` | ☑ 完整 |
| S2P 转换 | `VoicebankSession::convertS2p()` | ☑ 完整 |
| 音素校验 | `VoicebankSession::validatePhonemes()` | ☑ 完整 |
| 推理 handle | `VoicebankSession::ensureModelSet()` / `ModelSetHandle::load/start/stop/unload` | ☑ 完整 |
| G2P ONNX driver | `srt::g2p::setupG2pOnnxDriver()`（A1 已新增） | ☑ 完整 |
| ONNX 推理 driver | `srt::driver::setupOnnxInferenceDriver()` | ☑ 完整 |
| 音频 I/O | `srt::io::audio::*` | ☑ 完整 |
| 特征抽取 | `srt::feature::*` / `Extractor` 系列 | ☑ 完整 |

**结论**：synthrt API 表面完整，无关键缺口。

---

## 4. 修订记录

| 日期 | 版本 | 说明 |
|---|---|---|
| 2026-07-26 | v1 | 初稿：M1 已核实（非缺口）、M2 已驳回（M1 不成立）；其他 API 已扫描无缺口 |
