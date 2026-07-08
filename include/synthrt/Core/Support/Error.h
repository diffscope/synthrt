#ifndef SRT_CORE_SUPPORT_ERROR_H
#define SRT_CORE_SUPPORT_ERROR_H

#include <string>
#include <memory>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    class Error {
    public:
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

        inline Error() : Error(NoError) {
        }

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

        inline const Diagnostic &diagnostic() const {
            return *_diagnostic;
        }

        static Error success() {
            return Error(NoError);
        }

    protected:
        int _type;
        std::shared_ptr<std::string> _msg;
        std::shared_ptr<Diagnostic> _diagnostic;

        SRT_CORE_EXPORT static std::shared_ptr<std::string> defaultMessage(int type);
        SRT_CORE_EXPORT static std::shared_ptr<Diagnostic> defaultDiagnostic(int type,
                                                                             const std::string &message);
        SRT_CORE_EXPORT static int typeFromCode(ErrorCode code);
    };

}

#endif // SRT_CORE_SUPPORT_ERROR_H
