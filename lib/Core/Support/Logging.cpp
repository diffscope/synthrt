#include <synthrt/Core/Support/Logging.h>

namespace srt::core {

    // File-local callback storage: exists once per srt-core DLL instance,
    // shared by all DLLs that link to srt-core via the exported accessors below.
    // This fixes the Windows DLL boundary issue where header-only inline static
    // variables are duplicated per DLL, causing setLogCallback() calls from the
    // host application to be invisible to synthrt's own libraries.
    namespace {
        Logger::LogCallback &callbackStorage() {
            static Logger::LogCallback callback = nullptr;
            return callback;
        }
    }

    Logger::Logger(const char *file, int line, const char *function, const char *category)
        : _context(file, line, function, category) {}

    Logger::LogCallback Logger::logCallback() {
        return callbackStorage();
    }

    void Logger::setLogCallback(LogCallback callback) {
        callbackStorage() = callback;
    }

} // namespace srt::core
