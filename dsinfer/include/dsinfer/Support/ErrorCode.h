#ifndef DSINFER_ERRORCODE_H
#define DSINFER_ERRORCODE_H

#include <system_error>

#include <dsinfer/dsinfer_global.h>

namespace ds {

    /// The error kinds dsinfer raises, as a domain of its own.
    ///
    /// These reach a caller inside a \c srt::Error, which carries a \c std::error_code and so is
    /// not limited to synthrt's own kinds:
    ///
    /// \code
    ///     return srt::Error(ds::ErrorCode::NotInitialized, "acoustic session is not initialized");
    ///
    ///     if (err.code() == ds::ErrorCode::ShapeMismatch) { ... }
    /// \endcode
    ///
    /// Comparing through \c srt::Error::code() rather than \c type() is what keeps these apart
    /// from synthrt's codes of the same numeric value.
    enum class ErrorCode {
        /// Used before whatever had to be initialized was, or after it was closed.
        NotInitialized = 1,

        /// Opened something that already is.
        AlreadyOpen,

        /// The driver found does not implement the backend contract the inference requires.
        DriverMismatch,

        /// The backend's own runtime could not be loaded, or loaded but is unusable.
        DriverLoadFailed,

        /// The caller's input does not satisfy what the inference needs, such as a missing
        /// parameter or a speaker the input never names.
        InvalidInput,

        /// A tensor's element count is not what the stage expects it to be.
        ShapeMismatch,

        /// The session ran but handed back nothing usable.
        SessionFailed,

        /// A step inside dsinfer failed, such as building a tensor or resampling a curve.
        ProcessingFailed,
    };

    /// The category \c ErrorCode belongs to.
    DSINFER_EXPORT const std::error_category &error_category() noexcept;

    /// Bridges \c ErrorCode to \c std::error_code.
    ///
    /// Found through argument-dependent lookup, which is how \c std::error_code 's converting
    /// constructor reaches it.
    DSINFER_EXPORT std::error_code make_error_code(ErrorCode code) noexcept;

}

template <>
struct std::is_error_code_enum<ds::ErrorCode> : std::true_type {};

#endif // DSINFER_ERRORCODE_H
