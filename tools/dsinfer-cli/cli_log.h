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
    void installLogCallback();

    // --- Diagnostic logging for G2P/S2P pipeline (P0-3) ---
    // All at Debug level unless the result indicates a problem.

    /// Log G2P input (lyric, g2pId, context, version) at Debug level.
    void logG2pInput(const std::string &lyric, const std::string &g2pId,
                     const std::string &context, const stdc::VersionNumber &version);

    /// Log G2P output (pronunciation, mode). Warning for copy fallback,
    /// Error for failed, Debug for normal convert.
    void logG2pOutput(const std::string &pronunciation, const std::string &mode, bool failed);

    /// Log S2P input (pronunciation, s2pMode, s2pFile) at Debug level.
    void logS2pInput(const std::string &pronunciation, const std::string &s2pMode,
                     const std::filesystem::path &s2pFile);

    /// Log S2P output (phonemes, onsets) at Debug level.
    void logS2pOutput(const std::vector<std::string> &phonemes, const std::vector<bool> &onsets);

    /// Log buildWords summary (word count, note count) at Debug level.
    void logBuildWordsSummary(size_t wordCount, size_t noteCount);

} // namespace dsinfer_cli
