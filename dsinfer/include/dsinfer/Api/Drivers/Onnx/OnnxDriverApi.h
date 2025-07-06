#ifndef DSINFER_API_ONNX_ONNXDRIVERAPI_H
#define DSINFER_API_ONNX_ONNXDRIVERAPI_H

#include <filesystem>
#include <map>
#include <set>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>

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

    class SessionOpenArgs : public InferenceSessionOpenArgs {
    public:
        inline SessionOpenArgs() : InferenceSessionOpenArgs(API_NAME, API_VERSION) {
        }

        /// Whether to force the use of the CPU for the session.
        bool useCpu = false;
    };

    class SessionStartInput : public InferenceSessionStartInput {
    public:
        inline SessionStartInput() : InferenceSessionStartInput(API_NAME, API_VERSION) {
        }

        /// The input port names and the input tensors.
        std::map<std::string, srt::NO<ITensor>> inputs;

        /// The output port names.
        std::set<std::string> outputs;
    };

    class SessionResult : public InferenceSessionResult {
    public:
        inline SessionResult() : InferenceSessionResult(API_NAME, API_VERSION) {
        }

        std::map<std::string, srt::NO<ITensor>> outputs;
    };

}

#endif // DSINFER_API_ONNX_ONNXDRIVERAPI_H