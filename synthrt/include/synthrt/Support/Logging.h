#ifndef LOGGING_H
#define LOGGING_H

#include <stdcorelib/str.h>

#include <synthrt/synthrt_global.h>

namespace srt {

    class LogContext {
    public:
        inline LogContext() noexcept = default;
        inline LogContext(const char *fileName, int lineNumber, const char *functionName,
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

        inline Logger(LogContext context) : _context(std::move(context)) {
        }

        inline Logger(const char *file, int line, const char *function, const char *category)
            : _context(file, line, function, category) {
        }

        template <class... Args>
        inline void trace(const std::string_view &format, Args &&...args) {
            print(Trace, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void debug(const std::string_view &format, Args &&...args) {
            print(Debug, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void success(const std::string_view &format, Args &&...args) {
            print(Success, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void info(const std::string_view &format, Args &&...args) {
            print(Information, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void warning(const std::string_view &format, Args &&...args) {
            print(Warning, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void critical(const std::string_view &format, Args &&...args) {
            print(Critical, stdc::formatN(format, std::forward<Args>(args)...));
        }

        template <class... Args>
        inline void fatal(const std::string_view &format, Args &&...args) {
            print(Critical, stdc::formatN(format, std::forward<Args>(args)...));
            abort();
        }

        template <class... Args>
        inline void log(int level, const std::string_view &format, Args &&...args) {
            print(level, stdc::formatN(format, std::forward<Args>(args)...));
        }

        void print(int level, const std::string_view &message);

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
                 const std::string_view &format, Args &&...args) const {
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
                  Args &&...args) const {
            if (!isLevelEnabled(Level)) {
                return;
            }
            Logger(fileName, lineNumber, functionName, _name)
                .printf(Level, fmt, std::forward<Args>(args)...);
            if constexpr (Level == srt::Logger::Fatal) {
                Logger::abort();
            }
        }

        inline const LogCategory &_srtGetLogCategory() const {
            return *this;
        }

    protected:
        const char *_name;

        union {
            bool levelEnabled[8];
            uint64_t enabled;
        };
    };

}

static inline const srt::LogCategory &_srtGetLogCategory() {
    return srt::LogCategory::defaultCategory();
}

/*!
    \macro srtDebug
    \brief Logs a debug message to a log category.
    \code
        // User category
        srt::LogCategory cate("test");
        cate.setLevelEnabled(srt::Logger::Debug, true);
        cate.srtDebug("This is a debug message");
        cate.srtDebug("This is a debug message with arg: %1", 42);
        cate.srtDebugF("This is a debug message with arg: %d", 42);

        // Default category
        srtDebug("This is a debug message");
        srtDebug("This is a debug message with arg: %1", 42);
        srtDebug("This is a debug message with arg: %d", 42);
    \endcode
*/

#define srtLog(LEVEL, ...)                                                                         \
    _srtGetLogCategory().log<srt::Logger::LEVEL>(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define srtTrace(...)    srtLog(Trace, __VA_ARGS__)
#define srtDebug(...)    srtLog(Debug, __VA_ARGS__)
#define srtSuccess(...)  srtLog(Success, __VA_ARGS__)
#define srtInfo(...)     srtLog(Information, __VA_ARGS__)
#define srtWarning(...)  srtLog(Warning, __VA_ARGS__)
#define srtCritical(...) srtLog(Critical, __VA_ARGS__)
#define srtFatal(...)    srtLog(Critical, __VA_ARGS__)

#define srtLogF(LEVEL, ...)                                                                        \
    _srtGetLogCategory().logf<srt::Logger::LEVEL>(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define srtTraceF(...)    srtLogF(Trace, __VA_ARGS__)
#define srtDebugF(...)    srtLogF(Debug, __VA_ARGS__)
#define srtSuccessF(...)  srtLogF(Success, __VA_ARGS__)
#define srtInfoF(...)     srtLogF(Information, __VA_ARGS__)
#define srtWarningF(...)  srtLogF(Warning, __VA_ARGS__)
#define srtCriticalF(...) srtLogF(Critical, __VA_ARGS__)
#define srtFatalF(...)    srtLogF(Critical, __VA_ARGS__)

#endif // LOGGING_H