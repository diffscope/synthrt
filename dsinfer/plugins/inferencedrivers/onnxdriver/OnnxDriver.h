#ifndef DSINFER_ONNXDRIVER_H
#define DSINFER_ONNXDRIVER_H

#include <filesystem>

#include <dsinfer/Inference/InferenceDriver.h>

namespace ds {

    class OnnxDriver : public InferenceDriver {
    public:
        OnnxDriver();
        ~OnnxDriver();

    public:
        bool initialize(const srt::NO<InferenceDriverInitArgs> &args, srt::Error *error) override;

        srt::NO<InferenceSession> createSession() override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_ONNXDRIVER_H