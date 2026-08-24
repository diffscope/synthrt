#ifndef DSINFER_ONNXDRIVER_H
#define DSINFER_ONNXDRIVER_H

#include <dsinfer/Inference/InferenceDriver.h>

namespace ds {

    class OnnxDriver : public InferenceDriver {
    public:
        OnnxDriver();
        ~OnnxDriver();

        srt::Expected<void> initialize(const InferenceDriverInitArgs &args) override;
        std::unique_ptr<InferenceSession> createSession() override;

        const InferenceDriverExtension *extension() const override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_ONNXDRIVER_H
