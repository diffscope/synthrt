#ifndef LOGGING_H
#define LOGGING_H

#include <stdcorelib/str.h>

#include <synthrt/synthrt_global.h>

namespace srt {

    class LogContext {
    public:
        LogContext() noexcept = default;
        LogContext(const char *fileName, int lineNumber, const char *functionName,
                   const char *categoryName) noexcept
            : line(lineNumber), file(fileName), function(functionName), category(categoryName) {
        }

        int line = 0;
        const char *file = nullptr;
        const char *function = nullptr;
        const char *category = nullptr;
    };

    class SYNTHRT_EXPORT Logger {
    public:
        enum Level {
            Trace = 1,
            Debug,
            Success,
            Information,
            Warning,
            Critical,
            Fatal,
        };

        Logger(LogContext context) : _context(std::move(context)) {
        }

        Logger(const char *file, int line, const char *function, const char *category)
            : _context(file, line, function, category) {
        }

        template <class... Args>
        inline void trace(const std::string_view &format, Args &&...args) {
            println(Trace, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void debug(const std::string_view &format, Args &&...args) {
            println(Debug, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void success(const std::string_view &format, Args &&...args) {
            println(Success, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void info(const std::string_view &format, Args &&...args) {
            println(Information, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void warning(const std::string_view &format, Args &&...args) {
            println(Warning, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void critical(const std::string_view &format, Args &&...args) {
            println(Critical, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void fatal(const std::string_view &format, Args &&...args) {
            println(Critical, stdc::formatN(format, std::forward<Args>(args)...));
            abort();
        }

        template <class... Args>
        inline void log(int level, const std::string_view &format, Args &&...args) {
            println(level, stdc::formatN(format, std::forward<Args>(args)...));
        }

        void println(int level, const std::string_view &message);

        void printf(int level, const char *fmt, ...);

        static void abort();

    public:
        using LogCallback = void (*)(int, const LogContext &, const std::string_view &);

        static LogCallback logCallback();
        static void setLogCallback(LogCallback callback);

    protected:
        LogContext _context;
    };

    /// Yet another logging category implementation of Qt QLoggingCategory.
    class SYNTHRT_EXPORT LogCategory {
    public:
        explicit LogCategory(const char *name);
        ~LogCategory();

        inline const char *name() const {
            return _name;
        }
        inline bool isLevelEnabled(int level) const {
            return levelEnabled[level];
        }
        inline void setLevelEnabled(int level, bool enabled) {
            levelEnabled[level] = enabled;
        }

        using LogCategoryFilter = void (*)(LogCategory *);

        static LogCategoryFilter logFilter();
        static void setLogFilter(LogCategoryFilter filter);

        static std::string filterRules();
        void setFilterRules(const std::string &rules);

        static LogCategory &defaultCategory();

        template <int Level, class... Args>
        void log(const char *fileName, int lineNumber, const char *functionName,
                 const std::string_view &format, Args &&...args) {
            if (!isLevelEnabled(Level)) {
                return;
            }
            Logger(fileName, lineNumber, functionName, _name)
                .log(Level, format, std::forward<Args>(args)...);
            if constexpr (Level == srt::Logger::Fatal) {
                Logger::abort();
            }
        }

        template <int Level, class... Args>
        void logf(const char *fileName, int lineNumber, const char *functionName, const char *fmt,
                  Args &&...args) {
            if (!isLevelEnabled(Level)) {
                return;
            }
            Logger(fileName, lineNumber, functionName, _name)
                .printf(Level, fmt, std::forward<Args>(args)...);
            if constexpr (Level == srt::Logger::Fatal) {
                Logger::abort();
            }
        }

    protected:
        const char *_name;

        union {
            bool levelEnabled[8];
            uint64_t enabled;
        };
    };

}

/*!
    \macro srtCDebug
    \brief Logs a debug message to the user-defined log category.
    \code
        srt::LogCategory cate("test");
        cate.setLevelEnabled(srt::Logger::Debug, true);
        cate.srtCDebug("This is a debug message");
        cate.srtCDebug("This is a debug message with arg: %1", 42);
        cate.srtCDebugF("This is a debug message with arg: %d", 42);
    \endcode

    \macro srtDebug
    \brief Logs a debug message to the default log category.
    \code
        srtDebug("This is a debug message");
        srtDebug("This is a debug message with arg: %1", 42);
        srtDebug("This is a debug message with arg: %d", 42);
    \endcode
*/

#define srtCLog(LEVEL, ...) log<srt::Logger::LEVEL>(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define srtCTrace(...)      srtCLog(Trace, __VA_ARGS__)
#define srtCDebug(...)      srtCLog(Debug, __VA_ARGS__)
#define srtCSuccess(...)    srtCLog(Success, __VA_ARGS__)
#define srtCInfo(...)       srtCLog(Information, __VA_ARGS__)
#define srtCWarning(...)    srtCLog(Warning, __VA_ARGS__)
#define srtCCritical(...)   srtCLog(Critical, __VA_ARGS__)
#define srtCFatal(...)      srtCLog(Critical, __VA_ARGS__)

#define srtCLogF(LEVEL, ...) logf<srt::Logger::LEVEL>(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define srtCTraceF(...)      srtCLogF(Trace, __VA_ARGS__)
#define srtCDebugF(...)      srtCLogF(Debug, __VA_ARGS__)
#define srtCSuccessF(...)    srtCLogF(Success, __VA_ARGS__)
#define srtCInfoF(...)       srtCLogF(Information, __VA_ARGS__)
#define srtCWarningF(...)    srtCLogF(Warning, __VA_ARGS__)
#define srtCCriticalF(...)   srtCLogF(Critical, __VA_ARGS__)
#define srtCFatalF(...)      srtCLogF(Critical, __VA_ARGS__)

#define srtLog(LEVEL, ...) srt::LogCategory::defaultCategory().srtCLog(LEVEL, __VA_ARGS__)
#define srtTrace(...)      srt::LogCategory::defaultCategory().srtCTrace(__VA_ARGS__)
#define srtDebug(...)      srt::LogCategory::defaultCategory().srtCDebug(__VA_ARGS__)
#define srtSuccess(...)    srt::LogCategory::defaultCategory().srtCSuccess(__VA_ARGS__)
#define srtInfo(...)       srt::LogCategory::defaultCategory().srtCInfo(__VA_ARGS__)
#define srtWarning(...)    srt::LogCategory::defaultCategory().srtCWarning(__VA_ARGS__)
#define srtCritical(...)   srt::LogCategory::defaultCategory().srtCCritical(__VA_ARGS__)
#define srtFatal(...)      srt::LogCategory::defaultCategory().srtCFatal(__VA_ARGS__)

#define srtLogF(LEVEL, ...) srt::LogCategory::defaultCategory().srtCLogF(LEVEL, __VA_ARGS__)
#define srtTraceF(...)      srt::LogCategory::defaultCategory().srtCTraceF(__VA_ARGS__)
#define srtDebugF(...)      srt::LogCategory::defaultCategory().srtCDebugF(__VA_ARGS__)
#define srtSuccessF(...)    srt::LogCategory::defaultCategory().srtCSuccessF(__VA_ARGS__)
#define srtInfoF(...)       srt::LogCategory::defaultCategory().srtCInfoF(__VA_ARGS__)
#define srtWarningF(...)    srt::LogCategory::defaultCategory().srtCWarningF(__VA_ARGS__)
#define srtCriticalF(...)   srt::LogCategory::defaultCategory().srtCCriticalF(__VA_ARGS__)
#define srtFatalF(...)      srt::LogCategory::defaultCategory().srtCFatalF(__VA_ARGS__)

#endif // LOGGING_H