// LastError.cpp — TLS error buffer implementation

#include "LastError.h"

namespace srt::c::detail {

namespace {

// thread_local guarantees one buffer per OS thread; the C API never crosses
// thread boundaries for a single logical operation (caller responsibility).
thread_local std::string g_lastError;
thread_local srt_error g_lastErrorCode = SRT_OK;

} // namespace

void setLastError(std::string_view message) {
    g_lastError.assign(message.data(), message.size());
    g_lastErrorCode = SRT_ERR_GENERIC;
}

void setLastError(std::string_view message, srt_error code) {
    g_lastError.assign(message.data(), message.size());
    g_lastErrorCode = code;
}

void setLastError(const srt::core::Error &error) {
    if (error.ok()) {
        g_lastError.clear();
        g_lastErrorCode = SRT_OK;
        return;
    }
    // Store the full toString() output so the C caller can see the error
    // category, code, and source location — not just the bare message (BF-29).
    g_lastError = error.toString();
    g_lastErrorCode = mapErrorCode(error.code());
}

const char *lastErrorMessage() {
    // Always return a valid pointer; std::string::c_str() is never null.
    return g_lastError.c_str();
}

srt_error lastErrorCode() {
    return g_lastErrorCode;
}

void clearLastError() {
    g_lastError.clear();
    g_lastErrorCode = SRT_OK;
}

srt_error mapErrorCode(srt::core::ErrorCode code) {
    // Map ErrorCode → srt_error using the category-based approach (BF-25).
    // Using error.code() (ErrorCode) instead of error.type() (int) avoids the
    // G2P vs core Type enum value conflict.
    switch (srt::core::errorCodeCategory(code)) {
    case srt::core::ErrorCategory::None:
        return SRT_OK;
    case srt::core::ErrorCategory::General:
        switch (code) {
        case srt::core::ErrorCode::InvalidArgument:
        case srt::core::ErrorCode::InvalidFormat:
            return SRT_ERR_INVALID_ARG;
        case srt::core::ErrorCode::FileNotFound:
            return SRT_ERR_NOT_FOUND;
        case srt::core::ErrorCode::FileNotOpen:
        case srt::core::ErrorCode::FileDuplicated:
            return SRT_ERR_FILE_IO;
        case srt::core::ErrorCode::FeatureNotSupported:
        case srt::core::ErrorCode::NotImplemented:
            return SRT_ERR_UNSUPPORTED;
        case srt::core::ErrorCode::Timeout:
            return SRT_ERR_TIMEOUT;
        case srt::core::ErrorCode::Aborted:
            return SRT_ERR_ABORTED;
        case srt::core::ErrorCode::OutOfMemory:
            return SRT_ERR_OUT_OF_MEM;
        case srt::core::ErrorCode::RecursiveDependency:
            return SRT_ERR_DEPENDENCY_CYCLE;
        default:
            return SRT_ERR_GENERIC;
        }
    case srt::core::ErrorCategory::Package:
        switch (code) {
        case srt::core::ErrorCode::PackageDependencyCycle:
            return SRT_ERR_DEPENDENCY_CYCLE;
        case srt::core::ErrorCode::PackageVersionConflict:
            return SRT_ERR_LEVEL_MISMATCH;
        case srt::core::ErrorCode::PackageDependencyMissing:
        case srt::core::ErrorCode::PackageManifestNotFound:
            return SRT_ERR_NOT_FOUND;
        default:
            return SRT_ERR_FILE_IO;
        }
    case srt::core::ErrorCategory::Inference:
    case srt::core::ErrorCategory::G2P:
        switch (code) {
        case srt::core::ErrorCode::InferenceNotInitialized:
            return SRT_ERR_NOT_INIT;
        case srt::core::ErrorCode::G2pAlreadyInitialized:
            return SRT_ERR_ALREADY_INIT;
        default:
            return SRT_ERR_INIT_FAILED;
        }
    case srt::core::ErrorCategory::Driver:
        return SRT_ERR_INIT_FAILED;
    default:
        return SRT_ERR_GENERIC;
    }
}

srt_error mapError(const srt::core::Error &error) {
    // setLastError stores both the toString() message and the mapped code.
    setLastError(error);
    return g_lastErrorCode;
}

} // namespace srt::c::detail
