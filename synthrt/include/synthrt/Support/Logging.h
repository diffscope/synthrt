#ifndef SYNTHRT_LOGGING_H
#define SYNTHRT_LOGGING_H

#include <stdcorelib/support/logging.h>

#include <synthrt/synthrt_global.h>

namespace srt {

    /// The logging facility now lives in stdcorelib, which carries the implementation this header
    /// used to duplicate. These aliases keep the `srt::` spelling working; there is no separate
    /// type, so a `srt::LogCategory` and a `stdc::LogCategory` are one and the same and share the
    /// process-wide category registry, callback and filter rules.
    using LogContext = stdc::LogContext;

    using Logger = stdc::Logger;

    using LogCategory = stdc::LogCategory;

}

/*!
    \macro srtDebug
    \brief Logs a debug message to a log category.
    \code
        // User category
        srt::LogCategory lc("test");
        lc.setLevelEnabled(srt::Logger::Debug, true);
        lc.srtDebug("This is a debug message");
        lc.srtDebug("This is a debug message with arg: %1", 42);
        lc.srtDebugF("This is a debug message with arg: %d", 42);

        // Default category
        srtDebug("This is a debug message");
        srtDebug("This is a debug message with arg: %1", 42);
        srtDebugF("This is a debug message with arg: %d", 42);
    \endcode

    Each macro forwards to its stdcorelib counterpart, so both spellings may be mixed freely.
*/

#define srtLog(LEVEL, ...) stdcLog(LEVEL, __VA_ARGS__)
#define srtTrace(...)      stdcTrace(__VA_ARGS__)
#define srtDebug(...)      stdcDebug(__VA_ARGS__)
#define srtSuccess(...)    stdcSuccess(__VA_ARGS__)
#define srtInfo(...)       stdcInfo(__VA_ARGS__)
#define srtWarning(...)    stdcWarning(__VA_ARGS__)
#define srtCritical(...)   stdcCritical(__VA_ARGS__)
#define srtFatal(...)      stdcFatal(__VA_ARGS__)

#define srtLogF(LEVEL, ...) stdcLogF(LEVEL, __VA_ARGS__)
#define srtTraceF(...)      stdcTraceF(__VA_ARGS__)
#define srtDebugF(...)      stdcDebugF(__VA_ARGS__)
#define srtSuccessF(...)    stdcSuccessF(__VA_ARGS__)
#define srtInfoF(...)       stdcInfoF(__VA_ARGS__)
#define srtWarningF(...)    stdcWarningF(__VA_ARGS__)
#define srtCriticalF(...)   stdcCriticalF(__VA_ARGS__)
#define srtFatalF(...)      stdcFatalF(__VA_ARGS__)

#endif // SYNTHRT_LOGGING_H
