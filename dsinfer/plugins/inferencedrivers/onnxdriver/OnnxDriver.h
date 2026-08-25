#ifndef DSINFER_ONNXDRIVER_H
#define DSINFER_ONNXDRIVER_H

#include <memory>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriver.h>

namespace ds {

    namespace onnxdriver {

        class DriverContext;

    }

    /// Owns one ONNX Runtime configuration and creates sessions that share its model cache.
    class OnnxDriver : public InferenceDriver {
    public:
        OnnxDriver();
        ~OnnxDriver();

        /// Initializes the runtime and execution provider from \a args.
        srt::Expected<void> initialize(const InferenceDriverInitArgs &args) override;

        /// Creates a session after successful initialization.
        std::unique_ptr<InferenceSession> createSession() override;

        /// Returns the active ONNX driver extension, or null before initialization.
        const InferenceDriverExtension *extension() const override;

    private:
        std::shared_ptr<onnxdriver::DriverContext> m_context;
        Api::Onnx::DriverExtension m_extension;
    };

}

#endif // DSINFER_ONNXDRIVER_H
