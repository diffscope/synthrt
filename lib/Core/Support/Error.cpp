#include "Error.h"

namespace srt::core {

    std::shared_ptr<std::string> Error::defaultMessage(int type) {
        switch (type) {
            case NoError: {
                static auto message = std::make_shared<std::string>();
                return message;
            }
            case InvalidFormat: {
                static auto message = std::make_shared<std::string>("invalid format");
                return message;
            }
            case FileNotFound: {
                static auto message = std::make_shared<std::string>("file not found");
                return message;
            }
            case FileNotOpen: {
                static auto message = std::make_shared<std::string>("file not open");
                return message;
            }
            case FileDuplicated: {
                static auto message = std::make_shared<std::string>("file duplicated");
                return message;
            }
            case RecursiveDependency: {
                static auto message = std::make_shared<std::string>("recursive dependency");
                return message;
            }
            case FeatureNotSupported: {
                static auto message = std::make_shared<std::string>("feature not supported");
                return message;
            }
            case InvalidArgument: {
                static auto message = std::make_shared<std::string>("invalid argument");
                return message;
            }
            case NotImplemented: {
                static auto message = std::make_shared<std::string>("not implemented");
                return message;
            }
            case SessionError: {
                static auto message = std::make_shared<std::string>("session error");
                return message;
            }
            default:
                break;
        }
        static auto message = std::make_shared<std::string>("unknown error");
        return message;
    }

    std::shared_ptr<Diagnostic> Error::defaultDiagnostic(int type, const std::string &message) {
        auto diagnostic = std::make_shared<Diagnostic>();
        diagnostic->message = message;
        switch (type) {
            case NoError:
                diagnostic->code = ErrorCode::None;
                diagnostic->severity = Severity::Info;
                break;
            case InvalidFormat:
                diagnostic->code = ErrorCode::InvalidFormat;
                break;
            case FileNotFound:
                diagnostic->code = ErrorCode::FileNotFound;
                break;
            case FileNotOpen:
                diagnostic->code = ErrorCode::FileNotOpen;
                break;
            case FileDuplicated:
                diagnostic->code = ErrorCode::FileDuplicated;
                break;
            case RecursiveDependency:
                diagnostic->code = ErrorCode::RecursiveDependency;
                break;
            case FeatureNotSupported:
                diagnostic->code = ErrorCode::FeatureNotSupported;
                break;
            case InvalidArgument:
                diagnostic->code = ErrorCode::InvalidArgument;
                break;
            case NotImplemented:
                diagnostic->code = ErrorCode::NotImplemented;
                break;
            case SessionError:
                diagnostic->code = ErrorCode::SessionError;
                break;
            default:
                diagnostic->code = ErrorCode::InvalidArgument;
                break;
        }
        return diagnostic;
    }

    int Error::typeFromCode(ErrorCode code) {
        switch (code) {
            case ErrorCode::None:
                return NoError;
            case ErrorCode::InvalidFormat:
            case ErrorCode::PackageManifestInvalid:
            case ErrorCode::PackageManifestMissingField:
                return InvalidFormat;
            case ErrorCode::FileNotFound:
                return FileNotFound;
            case ErrorCode::FileNotOpen:
                return FileNotOpen;
            case ErrorCode::FileDuplicated:
                return FileDuplicated;
            case ErrorCode::RecursiveDependency:
                return RecursiveDependency;
            case ErrorCode::FeatureNotSupported:
                return FeatureNotSupported;
            case ErrorCode::NotImplemented:
                return NotImplemented;
            case ErrorCode::SessionError:
                return SessionError;
            case ErrorCode::InvalidArgument:
            case ErrorCode::PackageRootInvalid:
            case ErrorCode::PackageSourceAfterInitialize:
            case ErrorCode::PackageScanAfterInitialize:
                return InvalidArgument;
            default:
                return InvalidArgument;
        }
    }

}
