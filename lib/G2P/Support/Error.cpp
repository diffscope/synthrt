#include "Support/Error.h"

namespace srt::g2p {

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
