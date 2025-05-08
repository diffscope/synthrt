#include "Error.h"

namespace srt {

    std::string Error::defaultMessage(int type) {
        switch (type) {
            case NoError:
                break;
            case InvalidFormat:
                return "Invalid format";
            case FileNotFound:
                return "File not found";
            case FileDuplicated:
                return "File duplicated";
            case RecursiveDependency:
                return "Recursive dependency";
            case FeatureNotSupported:
                return "Feature not supported";
            case SessionError:
                return "Session error";
            default:
                break;
        }
        return "Unknown error";
    }

}