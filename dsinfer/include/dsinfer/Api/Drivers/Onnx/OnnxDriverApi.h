#ifndef DSINFER_API_ONNX_ONNXDRIVERAPI_H
#define DSINFER_API_ONNX_ONNXDRIVERAPI_H

#include <filesystem>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>

namespace ds::Api::Onnx {

    static const char API_NAME[] = "onnx";

    enum ExecutionProvider {
        CPUExecutionProvider = 0,
        CUDAExecutionProvider,
        DMLExecutionProvider,
        CoreMLExecutionProvider,
    };

    class DriverInitArgs : public InferenceDriverInitArgs {
    public:
        DriverInitArgs() : InferenceDriverInitArgs(API_NAME) {
        }

        /// The execution provider to use.
        ExecutionProvider ep = CPUExecutionProvider;

        /// The device index to use for CUDAExecutionProvider. (-1 means auto-select)
        int deviceIndex = -1;

        /// The onnxruntime library directory. (empty means use the default)
        std::filesystem::path runtimePath;
    };

    class SessionInitArgs : public InferenceSessionOpenArgs {
    public:
        SessionInitArgs() : InferenceSessionOpenArgs(API_NAME) {
        }

        /// Whether to force the use of the CPU for the session.
        bool useCpu = false;
    };

}

#endif // DSINFER_API_ONNX_ONNXDRIVERAPI_H