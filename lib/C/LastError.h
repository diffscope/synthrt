// LastError.h — internal TLS error buffer for the srt v4 C FFI layer
//
// The C API functions convert C++ errors (srt::core::Error / std::string)
// into C error codes and store the human-readable message in a thread-local
// buffer. The public srt_last_error() returns a pointer into this buffer.
//
// Thread-safety: each thread has its own buffer; no locking is required.

#ifndef SRT_C_LIB_LASTERROR_H
#define SRT_C_LIB_LASTERROR_H

#include <string>
#include <string_view>

#include <synthrt/C/srt.h>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>

namespace srt::c::detail {

    // Sets the per-thread last-error message from a raw string.
    // An empty view clears the buffer. The error code is set to
    // SRT_ERR_GENERIC (use the overload below for a specific code).
    void setLastError(std::string_view message);

    // Sets the per-thread last-error message and error code from raw values.
    void setLastError(std::string_view message, srt_error code);

    // Sets the per-thread last-error message and code from a srt::core::Error.
    // Stores error.toString() (including category, code, and source location).
    // NoError clears the buffer and resets the code to SRT_OK.
    void setLastError(const srt::core::Error &error);

    // Returns a pointer to the per-thread last-error message.
    // The pointer is valid until the next setLastError/clearLastError call on
    // the same thread; it always points at a valid (possibly empty) string.
    const char *lastErrorMessage();

    // Returns the per-thread last-error code.
    srt_error lastErrorCode();

    // Clears the per-thread last-error buffer and code.
    void clearLastError();

    // Maps a srt::core::ErrorCode to the public srt_error enum using the
    // category-based approach (see BF-25).
    srt_error mapErrorCode(srt::core::ErrorCode code);

    // Maps a srt::core::Error to the public srt_error enum.
    // Also stores the error message and code in the TLS buffer (convenience
    // wrapper used by all C API implementation files).
    srt_error mapError(const srt::core::Error &error);

} // namespace srt::c::detail

#endif // SRT_C_LIB_LASTERROR_H
