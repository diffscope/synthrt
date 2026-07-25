#include <synthrt/Core/Support/Logging.h>

#include <mutex>

namespace srt::core {

    // File-local callback storage: exists once per srt-core DLL instance,
    // shared by all DLLs that link to srt-core via the exported accessors below.
    // This fixes the Windows DLL boundary issue where header-only inline static
    // variables are duplicated per DLL, causing setLogCallback() calls from the
    // host application to be invisible to synthrt's own libraries.
    //
    // CODING-04 / ROBUST-04: concurrent read/write of std::function (here a
    // function pointer) is UB. A file-local mutex guards the callback storage.
    namespace {
        std::mutex &callbackMutex() {
            static std::mutex m;
            return m;
        }

        Logger::LogCallback &callbackStorage() {
            static Logger::LogCallback callback = nullptr;
            return callback;
        }
    }

    Logger::Logger(const char *file, int line, const char *function, const char *category)
        : _context(file, line, function, category) {}

    Logger::LogCallback Logger::logCallback() {
        std::lock_guard<std::mutex> lock(callbackMutex());
        return callbackStorage();
    }

    void Logger::setLogCallback(LogCallback callback) {
        std::lock_guard<std::mutex> lock(callbackMutex());
        callbackStorage() = callback;
    }

} // namespace srt::core
