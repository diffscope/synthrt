#ifndef SYNTHRT_ERROR_H
#define SYNTHRT_ERROR_H

#include <string>
#include <memory>

#include <synthrt/synthrt_global.h>

namespace srt {

    class Error {
    public:
        enum Type {
            NoError = 0,
            InvalidFormat,
            FileNotFound,
            FileDuplicated,
            RecursiveDependency,
            FeatureNotSupported,
            InvalidArgument,
            SessionError,
        };

        inline Error() : Error(NoError) {
        }

        inline Error(int type) : _type(type), _msg(defaultMessage(type)) {
        }

        inline Error(int type, std::string msg)
            : _type(type), _msg(std::make_shared<std::string>(std::move(msg))) {
        }

        inline Error(int type, const char *msg)
            : _type(type), _msg(std::make_shared<std::string>(msg)) {
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

    protected:
        int _type;
        std::shared_ptr<std::string> _msg;

        SYNTHRT_EXPORT static std::shared_ptr<std::string> defaultMessage(int type);
    };

}

#endif // SYNTHRT_ERROR_H