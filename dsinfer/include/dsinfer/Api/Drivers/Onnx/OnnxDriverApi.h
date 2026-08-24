#ifndef DSINFER_API_ONNX_ONNXDRIVERAPI_H
#define DSINFER_API_ONNX_ONNXDRIVERAPI_H

#include <filesystem>
#include <map>
#include <memory>
#include <set>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>

/// Declared rather than included, so that a caller uninterested in the extension below does not
/// pay for the onnxruntime headers.
struct OrtApi;
struct OrtApiBase;

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
        std::map<std::string, std::shared_ptr<ITensor>> inputs;

        /// The output port names.
        std::set<std::string> outputs;
    };

    class SessionResult : public InferenceSessionResult {
    public:
        inline SessionResult() : InferenceSessionResult(API_NAME, API_VERSION) {
        }

        std::map<std::string, std::shared_ptr<ITensor>> outputs;
    };

    /// What the driver loaded, for a caller that means to use the same runtime.
    ///
    /// dsinfer builds against ORT_API_MANUAL_INIT, so every module keeps an API pointer of its
    /// own. A host or a second plugin wanting the runtime this driver already opened has to hand
    /// this one to \c Ort::InitApi() rather than open the library a second time.
    class DriverExtension : public InferenceDriverExtension {
    public:
        inline DriverExtension() : InferenceDriverExtension(API_NAME, API_VERSION) {
        }

        /// Null until the driver's initialize() has succeeded.
        const OrtApi *ortApi = nullptr;
        const OrtApiBase *ortApiBase = nullptr;

        /// The \c ORT_API_VERSION the driver asked for. A caller compiled against a different one
        /// must not use \c ortApi.
        int ortApiVersion = 0;

        /// The shared library that was opened.
        std::filesystem::path runtimePath;

        ExecutionProvider ep = CPUExecutionProvider;
        int deviceIndex = -1;
    };

}

#endif // DSINFER_API_ONNX_ONNXDRIVERAPI_H
