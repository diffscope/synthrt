# Bug 清单与核实记录

日期: 2026-07-26（v1 初稿）
范围: 仅本项目（synthrt）生产代码与文档；lite 侧 bug 不在范围
原则: 每条 Bug 必须经源码核实后再下结论；未确认项标注"待核实"，不臆断修复

---

## 1. 核实流程

每条 Bug 按以下步骤核实：

1. **定位**：通过 grep / Read 精确定位代码行号与上下文
2. **对照准则**：依据 [design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) ARCH/ROBUST/CODING 检验是否违反
3. **调用方影响**：核查 lite 真实调用方是否触发该路径
4. **结论**：明确为 Bug / 非 Bug / 待核实，并给出修复方案或驳回理由
5. **执行**：Bug 修复单独 `git commit`；非 Bug 在本文档记录结论，不提交代码

---

## 2. Bug 清单

### B1: module docs 中 `ds::lang::` 失效引用 — ☑ 与 N1 合并

**结论**：文档层 bug（namespace 引用与源码不一致）。与 [01-code-normalization.md N1](file:///d:/projects/synthrt/docs/refactor/01-code-normalization.md) 同因，合并提交。

**核实记录**：
- `grep -r "ds::lang" include/ lib/ domains/ plugins/` 无源码命中 → `ds::lang` forwarding shim 已在历史重构中删除
- 3 个 module docs 仍引用该 namespace，导致文档与代码不一致：
  | 文件 | 行号 | 失效引用 |
  |---|---|---|
  | [docs/modules/overview.md#L121](file:///d:/projects/synthrt/docs/modules/overview.md) | 121 | `ds::lang::LanguageService langSvc;` |
  | [docs/modules/g2p.md#L142](file:///d:/projects/synthrt/docs/modules/g2p.md) | 142 | `namespace ds::lang;` |
  | [docs/modules/c-abi.md#L116](file:///d:/projects/synthrt/docs/modules/c-abi.md) | 116 | `ds::lang::LanguageService langSvc;` |

**修复方案**：见 [01-code-normalization.md N1](file:///d:/projects/synthrt/docs/refactor/01-code-normalization.md) §N1 修复方案。

---

### B2: docs/lite-integration/ 误删恢复 — ☑ 已完成

**结论**：非代码 bug；git 操作误删，已恢复。

**核实记录**：
- 在前序会话中，`docs/lite-integration/` 下 6 个文件被误标记为删除
- 已通过 `git restore --staged --worktree docs/lite-integration/` 恢复
- 无需提交（恢复到 HEAD 状态）

**状态**：☑ 已完成（无 commit）

---

### B3: VoicebankSession.cpp:893 `catch(...)` 是否设置错误 — ☑ 已核实（无 Bug）

**结论**：**非 Bug**。`catch(...)` 正确设置了 `S2pConversionFailed` 错误码，符合 CODING-02 异常边界要求。

**核实记录**：

[VoicebankSession.cpp#L880-L900](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp) `convertS2p` 的 try/catch 结构：

```cpp
try {
    const auto syllable = resource->convert(pronunciation);
    S2pResult out;
    out.phonemes = syllable.phonemes;
    out.onsets = syllable.onsets;
    return out;
} catch (const std::exception &e) {
    return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed, ...)
            .withExtraContext({...});
} catch (...) {
    return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                            "VoicebankSession::convertS2p: unknown S2P conversion failure")
            .withExtraContext({...});
}
```

`catch(...)` 块**返回了 `S2pConversionFailed` 错误**，并附带 `packageId/singerId/version/language` 上下文，符合 CODING-02 "第三方边界 try-catch 转换为 Error"。

**全量扫描**：对 [VoicebankSession.cpp](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp) 全部 3 处 `catch(...)` 核实，均正确处理：
| 行号 | 函数 | 处理方式 | 结论 |
|---|---|---|---|
| 449 | refresh callback | `SessionLog.srtWarning(...)` 记录日志 | ☑ 符合 ROBUST-05 |
| 645 | performRefresh | 设置 `r.errorMessage = "unknown voicebank refresh failure"` | ☑ 符合 CODING-02 |
| 893 | convertS2p | 返回 `S2pConversionFailed` Error + 上下文 | ☑ 符合 CODING-02 |

**状态**：☑ 已核实，无 Bug，不提交修复。

---

### B4: `convertG2p` 缺少 try/catch 异常边界 — ☑ 已修复（commit 2760ffb）

**结论**：**真实 Bug**（与 `convertS2p` 异常边界处理不一致，违反 CODING-02）。已修复。

**核实记录**：

[VoicebankSession.cpp#L813-L844](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp) `convertG2p` 直接返回 `svc->convert(...)`，**无 try/catch 包裹**：

```cpp
srt::core::Expected<std::vector<srt::g2p::G2pRes>>
    VoicebankSession::convertG2p(const ds::bank::SingerRef &singerKey,
                                 const std::string &language,
                                 const std::vector<srt::g2p::G2pInput> &inputs) const {
    // ... 前置检查（snapshot/singer/svc）均返回 Error ...
    const auto version = stdc::VersionNumber::fromString(singerKey.version);
    return svc->convert(singerKey.packageId, version, singerKey.singerId, language, inputs);
    // ↑ 无 try/catch；与 convertS2p 的处理不一致
}
```

**对比 `convertS2p`**（[VoicebankSession.cpp#L880-L900](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp)）：`resource->convert(pronunciation)` 被 try/catch 包裹，捕获 `std::exception` 与 `...` 并转换为 `S2pConversionFailed`。

**调用链分析**：

```
VoicebankSession::convertG2p            (无 try/catch)
  └─ srt::g2p::LanguageService::convert  (无 try/catch)
       └─ LanguageService::convertLyric  (无 try/catch)
            └─ srt::g2p::Manager::convert (无 try/catch)
                 └─ taskObj->start(_input)  ← ONNX 推理，可能抛异常
```

[Manager.cpp#L356-L490](file:///d:/projects/synthrt/lib/G2P/Core/Manager.cpp) `Manager::convert` 内部：
- 对 `start()` 返回值用 `if (!resultExp)` 检查 Error 路径
- 但**未 try/catch** `start()` 内部抛出的异常（如 ONNX runtime C++ 异常、`std::bad_alloc` 等）

**潜在风险**：
- 若 ONNX 推理抛异常（如模型损坏、内存不足、CUDA 错误），异常会穿越 `Manager::convert` → `LanguageService::convert` → `VoicebankSession::convertG2p` → lite 调用方
- 违反 CODING-02 "第三方边界 try-catch 转换为 Error"
- lite 侧 `G2pService` / `GetPronunciationTask` 可能未预期该异常，导致进程崩溃

**待核实项**（执行前必须确认）— ☑ 已全部核实：

1. `Task::start()` 是否声明 `noexcept`？— ☑ 已核实：[ITask.h#L68](file:///d:/projects/synthrt/include/synthrt/Core/Task/ITask.h) `virtual Expected<NO<TaskResult>> start(...) = 0;` **未声明 noexcept**，可能抛异常
2. `Manager::convert` 是否有上层调用方已 catch？— ☑ 已核实：调用链 `convertG2p` → `LanguageService::convert` → `convertLyric` → `Manager::convert` → `Task::start()` **均无 try/catch**，异常会穿越到 lite
3. `LanguageService::convert` 的其他重载是否有 try/catch？— ☑ 已核实：version-aware 重载（[LanguageServiceLang.cpp#L930-L956](file:///d:/projects/synthrt/lib/G2P/LanguageServiceLang.cpp)）与 deprecated 重载（[LanguageServiceLang.cpp#L958-L966](file:///d:/projects/synthrt/lib/G2P/LanguageServiceLang.cpp)）**均无 try/catch**

**核实结论**：B4 确认为真实 Bug。`Task::start()` 非 noexcept，调用链上无 try/catch，违反 CODING-02。

**修复方案**（已执行，commit 2760ffb）：

在 [VoicebankSession.cpp#L848-L866](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp) `convertG2p` 的 `svc->convert(...)` 外加 try/catch，mirror `convertS2p` 的异常边界模式：

```cpp
const auto version = stdc::VersionNumber::fromString(singerKey.version);
try {
    return svc->convert(singerKey.packageId, version, singerKey.singerId, language, inputs);
} catch (const std::exception &e) {
    return srt::core::Error(srt::core::ErrorCode::G2pConversionFailed,
                            std::string("VoicebankSession::convertG2p: ") + e.what())
            .withExtraContext({{"packageId", singerKey.packageId},
                               {"singerId", singerKey.singerId},
                               {"version", singerKey.version},
                               {"language", language}});
} catch (...) {
    return srt::core::Error(srt::core::ErrorCode::G2pConversionFailed,
                            "VoicebankSession::convertG2p: unknown G2P conversion failure")
            .withExtraContext({{"packageId", singerKey.packageId},
                               {"singerId", singerKey.singerId},
                               {"version", singerKey.version},
                               {"language", language}});
}
```

**注意**：
- ☑ 已确认 `ErrorCode::G2pConversionFailed` 存在（[Diagnostic.h#L107](file:///d:/projects/synthrt/include/synthrt/Core/Support/Diagnostic.h)）
- ☑ 已确认 `Error::withExtraContext` 签名与 `convertS2p` 一致（对照 [VoicebankSession.cpp#L889-L892](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp)）
- ☑ 已验证编译通过（clion mcp get_file_problems 无错误）
- ☑ 已验证回归测试通过（tst-ds-session-edge：110 assertions / 18 cases 全过，2 skipped）
- T3 单元测试（模拟 `Manager::convert` 抛异常）待补，详见 [04-test-coverage.md](file:///d:/projects/synthrt/docs/refactor/04-test-coverage.md) T3

**状态**：☑ 已修复（commit 2760ffb）

---

## 3. 驳回记录

（暂无）

---

## 4. 修订记录

| 日期 | 版本 | 说明 |
|---|---|---|
| 2026-07-26 | v1 | 初稿：B1/B2/B3 已下结论；B4 待核实 |
| 2026-07-26 | v2 | B4 已核实（Task::start() 非 noexcept）+ 已修复（commit 2760ffb）；回归测试通过 |
