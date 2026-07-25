#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Logging.h>

namespace dsinfer_cli {

    /// Global CLI log category, forwarded to the framework logger.
    extern srt::core::LogCategory cliLog;

    /// Install the log callback that forwards framework logs to the console
    /// (colored), mirroring ds-editor-lite's log_report_callback.
    //
    // TD-CLI-04: Idempotent—repeated calls are no-ops after the first install.
    // Use uninstallLogCallback() to reset the installed state (e.g. for tests
    // that need to swap or clear the callback between cases).
    void installLogCallback();

    /// Uninstall the CLI log callback, clearing the framework-level callback.
    // TD-CLI-04: Provided for test scenarios that need to reset the callback
    // state between cases. After this call, installLogCallback() will
    // re-install the callback on the next invocation.
    void uninstallLogCallback();

    // --- Diagnostic logging for G2P/S2P pipeline (P0-3) ---
    //
    // TD-CLI-09: P0-3 诊断日志级别约定（与 cli_log.cpp 实现严格对应）：
    //
    //   | 函数               | 默认级别    | 升级条件                         |
    //   |--------------------|-------------|----------------------------------|
    //   | logG2pInput        | Debug (D)   | 无                               |
    //   | logG2pOutput       | Debug (D)   | copy fallback → Warning (W)      |
    //   |                    |             | failed=true → Critical (C)       |
    //   | logS2pInput        | Debug (D)   | 无                               |
    //   | logS2pOutput       | Debug (D)   | 无                               |
    //   | logBuildWordsSummary| Debug (D)  | 无                               |
    //
    // BUG-CLI-018: cli_log.cpp 的 filter 等级从 Logger::Success 改为
    // Logger::Debug，确保上述 Debug 级诊断日志在 CLI 运行时可见。
    //
    // ROBUST-02: 所有 P0-3 函数通过 cliLog.srtXxx 调用，框架不会因日志
    // 回调抛异常而崩溃（log_report_callback 内部 try-catch）。

    /// Log G2P input (lyric, g2pId, context, version) at Debug level.
    ///
    /// Level: Debug (D). Never upgraded.
    void logG2pInput(const std::string &lyric, const std::string &g2pId,
                     const std::string &context, const stdc::VersionNumber &version);

    /// Log G2P output (pronunciation, mode, failed).
    ///
    /// Level: Debug (D) by default. Upgraded based on \p failed and \p mode:
    ///   - \c failed=true → Critical (C)  [conversion threw exception]
    ///   - \c mode == kG2pModeCopy → Warning (W)  [copy fallback, no G2P model]
    ///   - otherwise → Debug (D)  [normal conversion]
    void logG2pOutput(const std::string &pronunciation, const std::string &mode, bool failed);

    /// Log S2P input (pronunciation, s2pMode, s2pFile) at Debug level.
    ///
    /// Level: Debug (D). Never upgraded.
    void logS2pInput(const std::string &pronunciation, const std::string &s2pMode,
                     const std::filesystem::path &s2pFile);

    /// Log S2P output (phonemes, onsets) at Debug level.
    ///
    /// Level: Debug (D). Never upgraded.
    /// Phonemes with onset flag are formatted as "ph*" in the log message.
    void logS2pOutput(const std::vector<std::string> &phonemes, const std::vector<bool> &onsets);

    /// Log buildWords summary (word count, note count) at Debug level.
    ///
    /// Level: Debug (D). Never upgraded.
    void logBuildWordsSummary(size_t wordCount, size_t noteCount);

} // namespace dsinfer_cli
