#ifndef DSINFER_API_ONNX_ONNXDRIVERAPI_H
#define DSINFER_API_ONNX_ONNXDRIVERAPI_H

#include <filesystem>
#include <map>
#include <set>
#include <variant>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>

namespace ds::Api::Onnx {

    static const char API_NAME[] = "onnx";

    static const int API_VERSION = 1;

    enum ExecutionProvider {
        CPUExecutionProvider = 0,
        CUDAExecutionProvider,
        DMLExecutionProvider,
        CoreMLExecutionProvider,
    };

    class DriverInitArgs : public InferenceDriverInitArgs {
    public:
        DriverInitArgs() : InferenceDriverInitArgs(API_NAME, API_VERSION) {
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
        SessionOpenArgs() : InferenceSessionOpenArgs(API_NAME, API_VERSION) {
        }

        /// Whether to force the use of the CPU for the session.
        bool useCpu = false;
    };

    class Tensor {
    public:
        enum DataType {
            Float = 1,
            Bool,
            Int64,
        };

        std::vector<uint8_t> data;
        DataType dataType = DataType::Float;
        std::vector<int> shape;
    };

    class SessionStartInput : public InferenceSessionStartInput {
    public:
        SessionStartInput() : InferenceSessionStartInput(API_NAME, API_VERSION) {
        }

        /// The input port names and the input tensors.
        std::map<std::string, Tensor> inputs;

        /// The output port names.
        std::set<std::string> outputs;
    };

    class SessionResult : public InferenceSessionResult {
    public:
        SessionResult() : InferenceSessionResult(API_NAME, API_VERSION) {
        }

        /// The output might be one of the 3 types:
        /// - std::monostate: the output is not available (e.g. due to an error)
        /// - Tensor        : raw tensor data
        /// - Object        : an intermediate object (e.g. an Ort::Value)
        using Output = std::variant<std::monostate, Tensor, srt::NO<srt::Object>>;

        std::map<std::string, Output> outputs;
    };

}

#endif // DSINFER_API_ONNX_ONNXDRIVERAPI_H