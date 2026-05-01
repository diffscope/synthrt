#ifndef DSINFER_API_ONNX_ONNXDRIVERAPI_H
#define DSINFER_API_ONNX_ONNXDRIVERAPI_H

#include <filesystem>

#include <dsinfer/Api/Drivers/Common/CommonDriverApi.h>

namespace ds::Api::Onnx {

    inline constexpr char API_NAME[] = "onnx";

    inline constexpr int API_VERSION = 1;

    enum ExecutionProvider {
        CPUExecutionProvider = 0,
        CUDAExecutionProvider,
        DMLExecutionProvider,
        CoreMLExecutionProvider,
    };

    class DriverInitArgs : public InferenceDriverInitArgs {
    public:
        inline DriverInitArgs() : InferenceDriverInitArgs(API_NAME, API_VERSION) {
        }

        /// The execution provider to use.
        ExecutionProvider ep = CPUExecutionProvider;

        /// The device index to use for CUDAExecutionProvider. (-1 means auto-select)
        int deviceIndex = -1;

        /// The onnxruntime library directory. (empty means use the default)
        std::filesystem::path runtimePath;
    };

    class SessionOpenArgs : public Common::SessionOpenArgs {
    public:
        inline SessionOpenArgs() : Common::SessionOpenArgs() {
            setObjectName(API_NAME);
        }
    };

    using SessionStartInput = Common::SessionStartInput;

    using SessionResult = Common::SessionResult;

}

#endif // DSINFER_API_ONNX_ONNXDRIVERAPI_H