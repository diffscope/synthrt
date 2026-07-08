// LastError.cpp — TLS error buffer implementation

#include "LastError.h"

namespace srt::c::detail {

namespace {

// thread_local guarantees one buffer per OS thread; the C API never crosses
// thread boundaries for a single logical operation (caller responsibility).
thread_local std::string g_lastError;

} // namespace

void setLastError(std::string_view message) {
    g_lastError.assign(message.data(), message.size());
}

void setLastError(const srt::core::Error &error) {
    if (error.ok()) {
        g_lastError.clear();
        return;
    }
    g_lastError = error.message();
}

const char *lastErrorMessage() {
    // Always return a valid pointer; std::string::c_str() is never null.
    return g_lastError.c_str();
}

void clearLastError() {
    g_lastError.clear();
}

srt_error mapError(const srt::core::Error &error) {
    // Always store the message first so the caller can read it via
    // srt_last_error() even when the mapped code is SRT_OK.
    setLastError(error);

    if (error.ok()) {
        return SRT_OK;
    }

    switch (error.type()) {
    case srt::core::Error::FileNotFound:
        return SRT_ERR_NOT_FOUND;
    case srt::core::Error::FileNotOpen:
    case srt::core::Error::FileDuplicated:
        return SRT_ERR_FILE_IO;
    case srt::core::Error::InvalidFormat:
    case srt::core::Error::InvalidArgument:
        return SRT_ERR_INVALID_ARG;
    case srt::core::Error::FeatureNotSupported:
    case srt::core::Error::NotImplemented:
        return SRT_ERR_UNSUPPORTED;
    case srt::core::Error::SessionError:
        return SRT_ERR_GENERIC;
    case srt::core::Error::RecursiveDependency:
        return SRT_ERR_DEPENDENCY_CYCLE;
    case srt::core::Error::NoError:
        return SRT_OK;
    default:
        return SRT_ERR_GENERIC;
    }
}

} // namespace srt::c::detail
