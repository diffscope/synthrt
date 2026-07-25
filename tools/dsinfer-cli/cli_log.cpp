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

    // TD-CLI-04: Track whether the CLI log callback has been installed.
    // installLogCallback() is idempotent—repeated calls are no-ops after
    // the first install. uninstallLogCallback() resets this flag so tests
    // can swap or clear the callback between cases.
    bool logCallbackInstalled = false;

    // Colored console forwarder, extracted verbatim from main.cpp's
    // log_report_callback (lines 495-564).
    void log_report_callback(int level, const srt::core::LogContext &ctx,
                             const std::string_view &msg) {
        using namespace srt;
        using namespace stdc;

        // 日志回调必须永不抛异常穿越框架边界（ROBUST-02）：
        // Logger::print 直接调用 cb 无 try-catch，回调可能从 C-style 边界
        // （如 ONNX Runtime 日志转发）触发，C++ 异常穿越为 UB。
        try {
            // BUG-CLI-018: 仅丢弃 Trace，保留 Debug 及以上。
            // 原先 `level < Logger::Success` 会丢弃所有 Debug 级日志，
            // 导致 P0-3 诊断日志基础设施（srtDebug）在 CLI 运行时完全不可见。
            if (level < Logger::Debug) {
                return;
            }

            // BUG-CLI-020: std::localtime 非线程安全且失败返回 nullptr。
            // Windows 用 localtime_s，POSIX 用 localtime_r；失败时跳过时间戳。
            std::time_t t = std::time(nullptr);
            std::tm tm;
#ifdef _WIN32
            if (localtime_s(&tm, &t) != 0) {
                return;
            }
#else
            if (!localtime_r(&t, &tm)) {
                return;
            }
#endif

            std::stringstream ss;
            ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
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
        } catch (const std::exception &e) {
            // ROBUST-05: 显式报错，禁止错误吞没
            std::fprintf(stderr, "log callback failed: %s\n", e.what());
        } catch (...) {
            // 兜底非 std::exception 派生异常，静默吞没以保护回调边界
        }
    }

} // namespace

void installLogCallback() {
    if (logCallbackInstalled) {
        return;
    }
    srt::core::Logger::setLogCallback(log_report_callback);
    logCallbackInstalled = true;
}

void uninstallLogCallback() {
    // Passing nullptr clears the framework-level callback. This resets the
    // installed flag so installLogCallback() can re-install on next call.
    srt::core::Logger::setLogCallback(nullptr);
    logCallbackInstalled = false;
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
