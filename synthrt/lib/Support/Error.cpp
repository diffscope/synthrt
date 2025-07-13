#include "Error.h"

namespace srt {

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

}