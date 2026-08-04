#include "ErrorCode.h"

namespace ds {

    namespace {

        class ErrorCategory : public std::error_category {
        public:
            const char *name() const noexcept override {
                return "dsinfer";
            }

            std::string message(int value) const override {
                switch (static_cast<ErrorCode>(value)) {
                    case ErrorCode::NotInitialized:
                        return "not initialized";
                    case ErrorCode::AlreadyOpen:
                        return "already open";
                    case ErrorCode::DriverMismatch:
                        return "driver mismatch";
                    case ErrorCode::DriverLoadFailed:
                        return "driver load failed";
                    case ErrorCode::InvalidInput:
                        return "invalid input";
                    case ErrorCode::ShapeMismatch:
                        return "tensor shape mismatch";
                    case ErrorCode::SessionFailed:
                        return "session failed";
                    case ErrorCode::ProcessingFailed:
                        return "processing failed";
                }
                return "unknown error";
            }
        };

    }

    const std::error_category &error_category() noexcept {
        static const ErrorCategory instance;
        return instance;
    }

    std::error_code make_error_code(ErrorCode code) noexcept {
        return {static_cast<int>(code), error_category()};
    }

}
