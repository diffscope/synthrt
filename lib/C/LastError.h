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

#include <synthrt/Core/Support/Error.h>

namespace srt::c::detail {

    // Sets the per-thread last-error message from a raw string.
    // An empty view clears the buffer.
    void setLastError(std::string_view message);

    // Sets the per-thread last-error message from a srt::core::Error.
    // NoError clears the buffer.
    void setLastError(const srt::core::Error &error);

    // Returns a pointer to the per-thread last-error message.
    // The pointer is valid until the next setLastError/clearLastError call on
    // the same thread; it always points at a valid (possibly empty) string.
    const char *lastErrorMessage();

    // Clears the per-thread last-error buffer.
    void clearLastError();

    // Maps a srt::core::Error::Type to the public srt_error enum.
    // Also stores the error message in the TLS buffer (convenience wrapper
    // used by all C API implementation files).
    srt_error mapError(const srt::core::Error &error);

} // namespace srt::c::detail

#endif // SRT_C_LIB_LASTERROR_H
