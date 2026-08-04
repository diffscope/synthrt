#include "Error.h"

namespace srt {

    namespace {

        class ErrorCategory : public std::error_category {
        public:
            const char *name() const noexcept override {
                return "srt";
            }

            std::string message(int value) const override {
                return *Error::defaultMessage(value);
            }
        };

    }

    const std::error_category &Error::category() noexcept {
        static const ErrorCategory instance;
        return instance;
    }

    Error::Error(std::error_code ec)
        : _code(ec), _msg(std::make_shared<std::string>(ec ? ec.message() : std::string())) {
    }

    std::string Error::toString() const {
        std::string res;
        for (const Error *cur = this; cur; cur = cur->_cause.get()) {
            const auto &text = cur->message();
            if (text.empty()) {
                continue;
            }
            if (!res.empty()) {
                res += ": ";
            }
            res += text;
        }
        return res;
    }

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
            default:
                break;
        }
        static auto message = std::make_shared<std::string>("unknown error");
        return message;
    }

}