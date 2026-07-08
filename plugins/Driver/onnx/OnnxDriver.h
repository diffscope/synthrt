#ifndef SRT_DRIVER_ONNX_ONNXDRIVER_H
#define SRT_DRIVER_ONNX_ONNXDRIVER_H

#include <filesystem>

#include <synthrt/Driver/InferenceDriver.h>

namespace srt::driver::onnx {

    class OnnxDriver : public srt::driver::InferenceDriver {
    public:
        OnnxDriver();
        ~OnnxDriver();

    public:
        std::string arch() const override;
        std::string backend() const override;

        srt::core::Expected<void>
            initialize(const srt::core::NO<srt::driver::InferenceDriverInitArgs> &args) override;
        srt::core::NO<srt::driver::InferenceSession> createSession() override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // SRT_DRIVER_ONNX_ONNXDRIVER_H
