#pragma once

#include <memory>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    class Error {
    public:
        // === Legacy Type enum (deprecated, use ErrorCode) ===
        enum Type {
            NoError = 0,
            InvalidFormat,
            FileNotFound,
            FileNotOpen,
            FileDuplicated,
            RecursiveDependency,
            FeatureNotSupported,
            InvalidArgument,
            NotImplemented,
            SessionError,
        };

        // === Default constructor ===
        inline Error() : Error(NoError) {
        }

        // === New constructors (ErrorCode + auto source_location) ===
        inline Error(ErrorCode code, std::string msg,
                     const std::source_location &loc = std::source_location::current())
            : _type(typeFromCode(code)),
              _msg(std::make_shared<std::string>(std::move(msg))),
              _diagnostic(defaultDiagnostic(code, *_msg, loc)) {
        }

        inline Error(ErrorCode code, const char *msg,
                     const std::source_location &loc = std::source_location::current())
            : _type(typeFromCode(code)),
              _msg(std::make_shared<std::string>(msg)),
              _diagnostic(defaultDiagnostic(code, *_msg, loc)) {
        }

        inline Error(ErrorCode code, std::string msg, Diagnostic context,
                     const std::source_location &loc = std::source_location::current())
            : _type(typeFromCode(code)),
              _msg(std::make_shared<std::string>(std::move(msg))),
              _diagnostic(std::make_shared<Diagnostic>(std::move(context))) {
            _diagnostic->code = code;
            _diagnostic->message = *_msg;
            if (_diagnostic->location.empty()) {
                _diagnostic->location = formatLocation(loc);
            }
        }

        // === Legacy constructors (backward compat) ===
        inline explicit Error(int type) : Error(static_cast<Type>(type)) {
        }

        inline Error(Type type)
            : _type(type), _msg(defaultMessage(type)), _diagnostic(defaultDiagnostic(type, *_msg)) {
        }

        inline Error(int type, std::string msg)
            : _type(type), _msg(std::make_shared<std::string>(std::move(msg))),
              _diagnostic(defaultDiagnostic(type, *_msg)) {
        }

        inline Error(int type, const char *msg)
            : _type(type), _msg(std::make_shared<std::string>(msg)),
              _diagnostic(defaultDiagnostic(type, *_msg)) {
        }

        inline explicit Error(Diagnostic diagnostic)
            : _type(typeFromCode(diagnostic.code)),
              _msg(std::make_shared<std::string>(diagnostic.message)),
              _diagnostic(std::make_shared<Diagnostic>(std::move(diagnostic))) {
        }

        // === Query methods ===
        inline int type() const {
            return _type;
        }

        inline bool ok() const {
            return _type == NoError;
        }

        inline const std::string &message() const {
            return *_msg;
        }

        inline const char *what() const {
            return _msg->c_str();
        }

        inline ErrorCode code() const {
            return _diagnostic->code;
        }

        inline ErrorCategory category() const noexcept {
            return errorCodeCategory(_diagnostic->code);
        }

        inline const char *codeString() const noexcept {
            return errorCodeToString(_diagnostic->code);
        }

        inline const Diagnostic &diagnostic() const {
            return *_diagnostic;
        }

        /// Returns source location string "file:line:function", empty if not set.
        inline std::string sourceLocation() const {
            return _diagnostic->location;
        }

        /// Full error description: "[Category::Code] message\n  at file:line:function"
        SRT_CORE_EXPORT std::string toString() const;

        /// Concise error description: "[stage] msg at file:line:function".
        /// Includes stage context (moduleId) and location, but omits full
        /// trace and other context fields. Suitable for secondary error
        /// paths where toString() would be too verbose.
        SRT_CORE_EXPORT std::string messageWithLocation() const;

        // === Trace append (for cross-layer propagation) ===
        SRT_CORE_EXPORT void appendTrace(std::string entry);

        SRT_CORE_EXPORT void appendTrace(
            const std::source_location &loc = std::source_location::current(),
            std::string note = {});

        // === Chainable helpers (return *this for fluent propagation) ===

        /// Chainable wrapper of appendTrace, returns *this.
        SRT_CORE_EXPORT Error &withTrace(
            const std::source_location &loc = std::source_location::current(),
            std::string note = {});

        /// Chainable context setter: only assigns non-empty fields.
        SRT_CORE_EXPORT Error &withContext(std::string singerId = {},
                                           std::string moduleId = {},
                                           std::string packageId = {},
                                           std::string language = {});

        /// S5: Chainable extra-context appender. Adds key-value pairs to
        /// Diagnostic::extraContext for file-level and count-level diagnostics.
        /// Example: err.withExtraContext({{"stage", "convert"}, {"manifestFile", path}});
        SRT_CORE_EXPORT Error &withExtraContext(
            std::vector<std::pair<std::string, std::string>> entries);

        /// S5: Format all context fields (named + extraContext + trace) into a
        /// human-readable string for UI display. Differs from toString() which
        /// focuses on "[Category::Code] message at location".
        SRT_CORE_EXPORT std::string formatContext() const;

        // === Factory functions ===
        SRT_CORE_EXPORT static Error packageError(
            ErrorCode code, std::string msg, std::string packageId = {},
            const std::source_location &loc = std::source_location::current());

        SRT_CORE_EXPORT static Error inferenceError(
            ErrorCode code, std::string msg, std::string singerId = {},
            std::string stage = {},
            const std::source_location &loc = std::source_location::current());

        SRT_CORE_EXPORT static Error g2pError(
            ErrorCode code, std::string msg, std::string language = {},
            std::string packageId = {},
            const std::source_location &loc = std::source_location::current());

        static Error success() {
            return Error(NoError);
        }

    protected:
        int _type;
        std::shared_ptr<std::string> _msg;
        std::shared_ptr<Diagnostic> _diagnostic;

        SRT_CORE_EXPORT static std::shared_ptr<std::string> defaultMessage(int type);
        SRT_CORE_EXPORT static std::shared_ptr<Diagnostic> defaultDiagnostic(
            int type, const std::string &message);
        SRT_CORE_EXPORT static std::shared_ptr<Diagnostic> defaultDiagnostic(
            ErrorCode code, const std::string &message,
            const std::source_location &loc);
        SRT_CORE_EXPORT static int typeFromCode(ErrorCode code);
        SRT_CORE_EXPORT static std::string formatLocation(const std::source_location &loc);
    };

}
