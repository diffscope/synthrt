#ifndef DSINFER_API_ONNX_ONNXDRIVERAPI_H
#define DSINFER_API_ONNX_ONNXDRIVERAPI_H

#include <filesystem>
#include <map>
#include <memory>
#include <set>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>

/// Forward declarations keep ONNX Runtime headers out of clients that do not use DriverExtension.
struct OrtApi;
struct OrtApiBase;

namespace ds::Api::Onnx {

    /// Identifies the ONNX inference driver implementation.
    inline constexpr char DRIVER_NAME[] = "onnxdriver";

    /// Identifies the ONNX inference driver API.
    inline constexpr char API_NAME[] = "onnx";

    /// Identifies the version of the ONNX inference driver API.
    inline constexpr int API_VERSION = 1;

    /// Selects the backend used to execute ONNX models.
    enum class ExecutionProvider {
        CPU = 0, ///< Executes models with the ONNX Runtime CPU backend.
        CUDA,    ///< Executes models with the ONNX Runtime CUDA backend.
        DML,     ///< Executes models with the ONNX Runtime DirectML backend.
        CoreML,  ///< Executes models with the ONNX Runtime Core ML backend.
    };

    /// Configures initialization of the ONNX inference driver.
    class DriverInitArgs : public InferenceDriverInitArgs {
    public:
        inline DriverInitArgs() : InferenceDriverInitArgs(API_NAME, API_VERSION) {
        }

        /// Execution provider used by sessions created by the driver.
        ExecutionProvider ep = ExecutionProvider::CPU;

        /// Device selected for providers that expose multiple devices.
        ///
        /// A negative value requests automatic device selection.
        int deviceIndex = -1;

        /// Directory containing the ONNX Runtime shared library.
        ///
        /// An empty path requests the driver's default lookup behavior.
        std::filesystem::path runtimePath;
    };

    /// Configures one ONNX inference session.
    class SessionOpenArgs : public InferenceSessionOpenArgs {
    public:
        inline SessionOpenArgs() : InferenceSessionOpenArgs(API_NAME, API_VERSION) {
        }

        /// Forces this session to use the CPU provider when set.
        bool useCpu = false;
    };

    /// Supplies named tensors and requested outputs for one ONNX Runtime invocation.
    class SessionStartInput : public InferenceSessionStartInput {
    public:
        inline SessionStartInput() : InferenceSessionStartInput(API_NAME, API_VERSION) {
        }

        /// Maps model input names to their tensors.
        std::map<std::string, std::shared_ptr<ITensor>> inputs;

        /// Names the model outputs that the session must return.
        std::set<std::string> outputs;
    };

    /// Contains the named tensors produced by an ONNX Runtime invocation.
    class SessionResult : public InferenceSessionResult {
    public:
        inline SessionResult() : InferenceSessionResult(API_NAME, API_VERSION) {
        }

        /// Maps requested model output names to their tensors.
        std::map<std::string, std::shared_ptr<ITensor>> outputs;
    };

    /// Exposes the ONNX Runtime instance loaded by the driver.
    ///
    /// dsinfer uses \c ORT_API_MANUAL_INIT, so every module stores its own API pointer. A host or
    /// another plugin that shares this runtime must pass \c ortApi to \c Ort::InitApi(). It must
    /// not load a second copy of the shared library.
    class DriverExtension : public InferenceDriverExtension {
    public:
        inline DriverExtension() : InferenceDriverExtension(API_NAME, API_VERSION) {
        }

        /// ONNX Runtime API table, or null before driver initialization succeeds.
        const OrtApi *ortApi = nullptr;

        /// ONNX Runtime API bootstrap table, or null before driver initialization succeeds.
        const OrtApiBase *ortApiBase = nullptr;

        /// Value of \c ORT_API_VERSION requested by the driver.
        ///
        /// A client compiled for another API version must not use \c ortApi.
        int ortApiVersion = 0;

        /// Path of the ONNX Runtime shared library loaded by the driver.
        std::filesystem::path runtimePath;

        /// Execution provider selected when the driver was initialized.
        ExecutionProvider ep = ExecutionProvider::CPU;

        /// Device index selected when the driver was initialized.
        int deviceIndex = -1;
    };

}

#endif // DSINFER_API_ONNX_ONNXDRIVERAPI_H
