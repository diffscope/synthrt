#include "Support/Error.h"

namespace srt::g2p {

    namespace {
        // Maps legacy G2P Type values to the new ErrorCode (stable int values
        // since the Type enum is deprecated and frozen).
        ErrorCode mapTypeToCode(int type) {
            switch (type) {
                case 0:  return ErrorCode::G2pSuccess;
                case 1:  return ErrorCode::G2pConfigError;
                case 2:  return ErrorCode::G2pFileSystemError;
                case 3:  return ErrorCode::G2pDependencyError;
                case 4:  return ErrorCode::G2pRuntimeError;
                case 5:  return ErrorCode::G2pNotImplementedError;
                case 6:  return ErrorCode::G2pInitializationError;
                case 7:  return ErrorCode::G2pValidationError;
                case 8:  return ErrorCode::G2pNullPointerError;
                case 9:  return ErrorCode::G2pIndexError;
                case 10: return ErrorCode::G2pTimeoutError;
                case 11: return ErrorCode::G2pAlreadyInitialized;
                default: return ErrorCode::G2pRuntimeError;
            }
        }
    } // namespace

    // === New ErrorCode constructors (delegate to parent) ===

    Error::Error(ErrorCode code, std::string msg, const std::source_location &loc)
        : srt::core::Error(code, std::move(msg), loc) {}

    Error::Error(ErrorCode code, const char *msg, const std::source_location &loc)
        : srt::core::Error(code, msg, loc) {}

    Error::Error(ErrorCode code, std::string msg, std::string suggestion,
                 const std::source_location &loc)
        : srt::core::Error(code, std::move(msg), loc),
          _suggestion(std::make_shared<std::string>(std::move(suggestion))) {}

    Error::Error(ErrorCode code, const char *msg, const char *suggestion,
                 const std::source_location &loc)
        : srt::core::Error(code, msg, loc),
          _suggestion(std::make_shared<std::string>(suggestion)) {}

    // === Legacy Type-based constructors (deprecated, map Type → ErrorCode) ===

    Error::Error(Type type)
        : Error(mapTypeToCode(static_cast<int>(type)), *defaultMessage(type)) {}

    Error::Error(Type type, std::string msg)
        : Error(mapTypeToCode(static_cast<int>(type)), std::move(msg)) {}

    Error::Error(Type type, const char *msg)
        : Error(mapTypeToCode(static_cast<int>(type)), msg) {}

    Error::Error(Type type, std::string msg, std::string suggestion)
        : Error(mapTypeToCode(static_cast<int>(type)), std::move(msg), std::move(suggestion)) {}

    Error::Error(Type type, const char *msg, const char *suggestion)
        : Error(mapTypeToCode(static_cast<int>(type)), msg, suggestion) {}

    std::shared_ptr<std::string> Error::defaultMessage(Type type) {
        switch (type) {
            case Success: {
                static auto message = std::make_shared<std::string>();
                return message;
            }
            case ConfigError: {
                static auto message = std::make_shared<std::string>("configuration error");
                return message;
            }
            case FileSystemError: {
                static auto message = std::make_shared<std::string>("file system error");
                return message;
            }
            case DependencyError: {
                static auto message = std::make_shared<std::string>("dependency error");
                return message;
            }
            case RuntimeError: {
                static auto message = std::make_shared<std::string>("runtime error");
                return message;
            }
            case NotImplementedError: {
                static auto message = std::make_shared<std::string>("not implemented");
                return message;
            }
            case InitializationError: {
                static auto message = std::make_shared<std::string>("initialization error");
                return message;
            }
            case ValidationError: {
                static auto message = std::make_shared<std::string>("validation error");
                return message;
            }
            case NullPointerError: {
                static auto message = std::make_shared<std::string>("null pointer error");
                return message;
            }
            case IndexError: {
                static auto message = std::make_shared<std::string>("index error");
                return message;
            }
            case TimeoutError: {
                static auto message = std::make_shared<std::string>("timeout error");
                return message;
            }
            case AlreadyInitialized: {
                static auto message =
                    std::make_shared<std::string>("already initialized");
                return message;
            }
            default:
                break;
        }
        static auto message = std::make_shared<std::string>("unknown error");
        return message;
    }

} // namespace srt::g2p
