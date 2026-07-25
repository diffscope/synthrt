# 测试覆盖缺口与新增测试计划

日期: 2026-07-26（v1 初稿）
范围: 仅本项目（synthrt）测试覆盖；lite 侧测试不在范围
原则: 仅在规范化或 bug 修复引入新行为时补单测；不为了覆盖率而写测试

---

## 1. 现状

经 [05-verification-checklist.md](file:///d:/projects/synthrt/docs/lite-integration/05-verification-checklist.md) v8 状态：
- A1 `setupG2pOnnxDriver` 单元测试已通过（143 case / 141 passed + 2 skipped）
- A2 `VoicebankSnapshot` 查询方法单元测试已通过（11 case / 27 assertions）
- CTest 全量：1553 case / 29.31 sec，100% 通过
- C2 端到端冒烟待用户更新 vcpkg 后执行

---

## 2. 测试任务

### T1: 等待用户更新 vcpkg 后执行 C2 端到端冒烟 — ☐ 阻塞

**依赖**：用户更新 vcpkg 后解锁。

**验证项**（详见 [05-verification-checklist.md](file:///d:/projects/synthrt/docs/lite-integration/05-verification-checklist.md) §3）：
- lite 启动 + 单声库推理（Duration → Pitch → Variance → Acoustic → Vocoder）
- 多版本共存（v1.0.0 + v1.1.0 独立路由）
- 热添加声库（refresh 后新声库可用，原推理不受影响）
- stale 重试（refresh 进行中调用 `start()` → `StaleModelSet` → 一次重试成功）

**状态**：☐ 阻塞（等待 vcpkg 更新）

---

### T2: 评估 GameExtractor.cpp CODING-03 修复是否需补路径规范化单测 — ☐ 待评估

**依赖**：N2 执行后评估。

**核实内容**：

[GameExtractor.cpp#L104](file:///d:/projects/synthrt/plugins/Extract/game/GameExtractor.cpp) 与 [GameExtractor.cpp#L139](file:///d:/projects/synthrt/plugins/Extract/game/GameExtractor.cpp) 的 `.string()` 调用改为 `stdc::path::to_utf8()` 后，需评估：

1. **是否引入新行为**：
   - `path.string()` 在 Windows 上返回 native 编码（可能含 locale 依赖）
   - `stdc::path::to_utf8()` 返回 UTF-8 编码
   - 行为差异：非 ASCII 路径（如中文目录）的错误消息编码不同
2. **是否需补单测**：
   - 若 N2 修复后 GameExtractor 的错误消息在非 ASCII 路径下行为改变 → 需补单测
   - 若行为不变（仅编码规范化，不影响功能）→ 不补单测，由代码审查验证

**评估结论**（待 N2 执行后填写）：

| 项 | 结论 |
|---|---|
| N2 修复是否引入新行为？ | 待评估 |
| 是否需补单测？ | 待评估 |
| 单测文件路径（若需） | `plugins/Extract/game/unittests/tst_game_extractor_path.cpp`（候选） |

**状态**：☐ 待 N2 执行后评估

---

### T3: B4 修复（若执行）需补异常边界单测 — ☐ 条件性

**依赖**：B4 核实后若执行修复，则补单测。

**验证项**：
- 模拟 `Manager::convert` 抛 `std::exception` → `convertG2p` 返回 `G2pConversionFailed` Error，不崩溃
- 模拟 `Manager::convert` 抛未知异常 → `convertG2p` 返回 `G2pConversionFailed` Error（`catch(...)` 路径）
- 对照 `convertS2p` 的异常处理路径，验证两者行为一致

**单测文件路径（若需）**：`domains/ds-session/unittests/tst_vbs_g2p_exception.cpp`（候选）

**状态**：☐ 条件性（B4 执行后解锁）

---

## 4. 修订记录

| 日期 | 版本 | 说明 |
|---|---|---|
| 2026-07-26 | v1 | 初稿：T1 阻塞、T2 待评估、T3 条件性 |
