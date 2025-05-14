#include "Error.h"

namespace srt {

    std::shared_ptr<std::string> Error::defaultMessage(int type) {
        switch (type) {
            case NoError: {
                static auto message = std::make_shared<std::string>();
                return message;
            }
            case InvalidFormat: {
                static auto message = std::make_shared<std::string>("Invalid format");
                return message;
            }
            case FileNotFound: {
                static auto message = std::make_shared<std::string>("File not found");
                return message;
            }
            case FileDuplicated: {
                static auto message = std::make_shared<std::string>("File duplicated");
                return message;
            }
            case RecursiveDependency: {
                static auto message = std::make_shared<std::string>("Recursive dependency");
                return message;
            }
            case FeatureNotSupported: {
                static auto message = std::make_shared<std::string>("Feature not supported");
                return message;
            }
            case SessionError: {
                static auto message = std::make_shared<std::string>("Session error");
                return message;
            }
            default:
                break;
        }
        static auto message = std::make_shared<std::string>("Unknown error");
        return message;
    }

}