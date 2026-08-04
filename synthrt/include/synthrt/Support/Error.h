#ifndef SYNTHRT_ERROR_H
#define SYNTHRT_ERROR_H

#include <string>
#include <memory>
#include <system_error>

#include <synthrt/synthrt_global.h>

namespace srt {

    class Error {
    public:
        /// The error kinds synthrt itself raises.
        ///
        /// Libraries built on top of synthrt do not extend this enum. They register a
        /// \c std::error_category of their own and pass the resulting \c std::error_code to
        /// Error's constructor, which keeps their codes from colliding with these.
        enum ErrorCode {
            NoError = 0,
            InvalidFormat,
            FileNotFound,
            FileNotOpen,
            FileDuplicated,
            RecursiveDependency,
            FeatureNotSupported,
            InvalidArgument,
            NotImplemented,
        };

        /// The \c std::error_category that \c ErrorCode belongs to.
        SYNTHRT_EXPORT static const std::error_category &category() noexcept;

        Error();

        explicit Error(int code);

        Error(ErrorCode code);

        inline Error(ErrorCode code, std::string msg)
            : _code(static_cast<int>(code), category()),
              _msg(std::make_shared<std::string>(std::move(msg))) {
        }

        /// \a msg may be null, in which case the message is empty - constructing a std::string
        /// from a null pointer is undefined.
        inline Error(ErrorCode code, const char *msg)
            : _code(static_cast<int>(code), category()),
              _msg(std::make_shared<std::string>(msg ? msg : "")) {
        }

        /// Creates an error in an arbitrary domain, taking its text from \a ec 's category.
        SYNTHRT_EXPORT explicit Error(std::error_code ec);

        /// Creates an error in an arbitrary domain with an explicit \a msg.
        inline Error(std::error_code ec, std::string msg)
            : _code(ec), _msg(std::make_shared<std::string>(std::move(msg))) {
        }

        /// Returns the full error code, both value and domain.
        ///
        /// Prefer comparing against this over type(): \c std::error_code equality takes the
        /// category into account, so a code from another domain cannot collide with an
        /// \c ErrorCode of the same numeric value.
        ///
        /// \code
        ///     if (err.code() == Error::FileNotFound) { ... }
        /// \endcode
        inline const std::error_code &code() const {
            return _code;
        }

        /// Returns the numeric value of the error code.
        ///
        /// \warning Only meaningful together with the domain. Two errors from different
        ///          categories may well share a value, so comparing this against an
        ///          \c ErrorCode is only sound when the error is known to be synthrt's own.
        ///          Use code() instead.
        inline int type() const {
            return _code.value();
        }

        inline bool ok() const {
            return !_code;
        }

        /// Returns this error's own text, without anything from its cause.
        ///
        /// \sa toString(), which renders the whole chain.
        inline const std::string &message() const {
            return *_msg;
        }

        inline const char *what() const {
            return _msg->c_str();
        }

        static Error success();

    public:
        /// Returns the error this one arose from, or a success value when this is the root cause.
        inline Error cause() const {
            return _cause ? *_cause : Error();
        }

        /// Returns a copy of this error recording \a cause as what it arose from.
        ///
        /// A successful \a cause is ignored, so the result of a call that may or may not have
        /// failed can be passed straight in.
        ///
        /// \code
        ///     return Error(Error::FileNotOpen,
        ///                  stdc::formatN(R"(required package "%1" not valid)", dep.id))
        ///                .withCause(depPkg.error());
        /// \endcode
        inline Error withCause(Error cause) const {
            Error res = *this;
            if (!cause.ok()) {
                res._cause = std::make_shared<const Error>(std::move(cause));
            }
            return res;
        }

        /// Returns the innermost error of the chain - the one that actually failed. Returns a copy
        /// of this error when it has no cause.
        inline Error rootCause() const {
            const Error *cur = this;
            while (cur->_cause) {
                cur = cur->_cause.get();
            }
            return *cur;
        }

        /// Renders the whole chain, outermost first, joined with <tt>": "</tt>. Levels carrying no
        /// text of their own are skipped.
        ///
        /// \sa message(), which returns this error's own text only.
        SYNTHRT_EXPORT std::string toString() const;

        /// Returns the canned text for an \c ErrorCode, shared rather than allocated per error.
        ///
        /// \internal
        SYNTHRT_EXPORT static std::shared_ptr<std::string> defaultMessage(int code);

        /// Bridges \c ErrorCode to \c std::error_code.
        ///
        /// A hidden friend rather than a static member: \c std::error_code's converting
        /// constructor calls this unqualified and finds it through argument-dependent lookup,
        /// which considers namespace-scope friends of an associated class but never its static
        /// member functions.
        friend std::error_code make_error_code(ErrorCode code) noexcept {
            return {static_cast<int>(code), category()};
        }

    protected:
        std::error_code _code;
        std::shared_ptr<std::string> _msg;

        /// What this error arose from; null at the root of the chain.
        std::shared_ptr<const Error> _cause;
    };

}

template <>
struct std::is_error_code_enum<srt::Error::ErrorCode> : std::true_type {};

/// Catches the specialization above arriving too late to matter.
///
/// It can only follow the complete class, so anything inside \c Error that makes the compiler
/// choose between <tt>Error(ErrorCode)</tt> and <tt>Error(std::error_code)</tt> instantiates
/// \c std::is_error_code_enum_v first and freezes it at \c false. The implicit conversion then
/// silently stops working, and the compiler blames the call site rather than the cause. Members
/// that construct an \c Error from an \c ErrorCode therefore live below this line.
static_assert(std::is_error_code_enum_v<srt::Error::ErrorCode>,
              "srt::Error::ErrorCode lost its std::error_code conversion: something inside the "
              "class instantiated std::is_error_code_enum_v before the specialization was seen");

namespace srt {

    inline Error::Error() : Error(NoError) {
    }

    inline Error::Error(int code) : Error(static_cast<ErrorCode>(code)) {
    }

    inline Error::Error(ErrorCode code)
        : _code(static_cast<int>(code), category()),
          _msg(defaultMessage(static_cast<int>(code))) {
    }

    inline Error Error::success() {
        return Error(NoError);
    }

}

#endif // SYNTHRT_ERROR_H
