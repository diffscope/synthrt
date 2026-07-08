#include "cli_log.h"

#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <stdcorelib/console.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Base/LangCommon.h>

namespace dsinfer_cli {

srt::core::LogCategory cliLog("cli");

namespace {

    // Colored console forwarder, extracted verbatim from main.cpp's
    // log_report_callback (lines 495-564).
    void log_report_callback(int level, const srt::core::LogContext &ctx,
                             const std::string_view &msg) {
        using namespace srt;
        using namespace stdc;

        if (level < Logger::Success) {
            return;
        }

        auto t = std::time(nullptr);
        auto tm = std::localtime(&t);

        std::stringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
        auto dts = ss.str();

        int foreground, background;
        switch (level) {
            case Logger::Success:
                foreground = console::lightgreen;
                background = foreground;
                break;
            case Logger::Warning:
                foreground = console::yellow;
                background = foreground;
                break;
            case Logger::Critical:
            case Logger::Fatal:
                foreground = console::red;
                background = foreground;
                break;
            default:
                foreground = console::nocolor;
                background = console::white;
                break;
        }

        const char *sig;
        switch (level) {
            case Logger::Trace:
                sig = "T";
                break;
            case Logger::Debug:
                sig = "D";
                break;
            case Logger::Success:
                sig = "S";
                break;
            case Logger::Warning:
                sig = "W";
                break;
            case Logger::Critical:
                sig = "C";
                break;
            case Logger::Fatal:
                sig = "F";
                break;
            default:
                sig = "I";
                break;
        }
        console::printf(console::nostyle, foreground, console::nocolor, "[%s] %-15s",
                        dts.c_str(), ctx.category);
        console::printf(console::nostyle, console::nocolor, background, " %s ", sig);
        console::printf(console::nostyle, console::nocolor, console::nocolor, "  ");
        console::println(console::nostyle, foreground, console::nocolor, msg);
        // Flush so log lines appear immediately when stdout is piped/captured
        // (Windows uses full buffering for non-TTY stdout).
        std::fflush(stdout);
    }

} // namespace

void installLogCallback() {
    srt::core::Logger::setLogCallback(log_report_callback);
}

// --- Diagnostic logging implementations (P0-3) ---

void logG2pInput(const std::string &lyric, const std::string &g2pId,
                 const std::string &context, const stdc::VersionNumber &version) {
    cliLog.srtDebug(stdc::formatN("G2P input: lyric='%1', g2pId=%2, context='%3', version=%4",
                                  lyric, g2pId, context, version.toString()));
}

void logG2pOutput(const std::string &pronunciation, const std::string &mode, bool failed) {
    if (failed) {
        cliLog.srtCritical(stdc::formatN("G2P output: pronunciation='%1', mode=%2 [FAILED]",
                                         pronunciation, mode));
    } else if (mode == srt::g2p::kG2pModeCopy) {
        cliLog.srtWarning(stdc::formatN("G2P output: pronunciation='%1', mode=%2 [copy fallback]",
                                        pronunciation, mode));
    } else {
        cliLog.srtDebug(stdc::formatN("G2P output: pronunciation='%1', mode=%2",
                                      pronunciation, mode));
    }
}

void logS2pInput(const std::string &pronunciation, const std::string &s2pMode,
                 const std::filesystem::path &s2pFile) {
    cliLog.srtDebug(stdc::formatN("S2P input: pronunciation='%1', s2pMode=%2, s2pFile=%3",
                                  pronunciation, s2pMode, stdc::path::to_utf8(s2pFile)));
}

void logS2pOutput(const std::vector<std::string> &phonemes, const std::vector<bool> &onsets) {
    std::string phStr;
    for (size_t i = 0; i < phonemes.size(); ++i) {
        if (i > 0) phStr += " ";
        phStr += phonemes[i];
        if (i < onsets.size() && onsets[i]) phStr += "*";
    }
    cliLog.srtDebug(stdc::formatN("S2P output: phonemes=[%1]", phStr));
}

void logBuildWordsSummary(size_t wordCount, size_t noteCount) {
    cliLog.srtDebug(stdc::formatN("buildWords: %1 words from %2 notes", wordCount, noteCount));
}

} // namespace dsinfer_cli
