#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/str.h>

namespace srt::core {

    class LogContext {
    public:
        LogContext() noexcept = default;
        LogContext(const char *fileName, int lineNumber, const char *functionName,
                   const char *categoryName) noexcept
            : line(lineNumber), file(fileName), function(functionName), category(categoryName) {}

        int line = 0;
        const char *file = nullptr;
        const char *function = nullptr;
        const char *category = nullptr;
    };

    class Logger {
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

        using LogCallback = void (*)(int, const LogContext &, const std::string_view &);

        Logger(const char *file, int line, const char *function, const char *category)
            : _context(file, line, function, category) {}

        template <class... Args>
        void log(int level, const std::string_view &format, Args &&...args) {
            print(level, stdc::formatN(format, std::forward<Args>(args)...));
        }

        void print(int level, const std::string_view &message) {
            if (auto cb = logCallback()) {
                cb(level, _context, message);
            }
        }

        static LogCallback logCallback() {
            return callbackStorage();
        }

        static void setLogCallback(LogCallback callback) {
            callbackStorage() = callback;
        }

    private:
        static LogCallback &callbackStorage() {
            static LogCallback callback = nullptr;
            return callback;
        }

        LogContext _context;
    };

    class LogCategory {
    public:
        explicit LogCategory(const char *name) : _name(name) {}

        template <int Level, class... Args>
        void log(const char *fileName, int lineNumber, const char *functionName,
                 const std::string_view &format, Args &&...args) const {
            Logger(fileName, lineNumber, functionName, _name)
                .log(Level, format, std::forward<Args>(args)...);
        }

        const LogCategory &_srtGetLogCategory() const {
            return *this;
        }

    private:
        const char *_name;
    };

}

namespace srt {

    using core::LogCategory;
    using core::LogContext;
    using core::Logger;

}

#define srtLog(LEVEL, ...)                                                                         \
    _srtGetLogCategory().log<srt::core::Logger::LEVEL>(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define srtTrace(...)    srtLog(Trace, __VA_ARGS__)
#define srtDebug(...)    srtLog(Debug, __VA_ARGS__)
#define srtSuccess(...)  srtLog(Success, __VA_ARGS__)
#define srtInfo(...)     srtLog(Information, __VA_ARGS__)
#define srtWarning(...)  srtLog(Warning, __VA_ARGS__)
#define srtCritical(...) srtLog(Critical, __VA_ARGS__)
#define srtFatal(...)    srtLog(Fatal, __VA_ARGS__)
