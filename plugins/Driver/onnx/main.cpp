#include <synthrt/Driver/InferenceDriverPlugin.h>

#include "OnnxDriver.h"

namespace srt::driver::onnx {

    class OnnxDriverPlugin : public srt::driver::InferenceDriverPlugin {
    public:
        OnnxDriverPlugin() = default;

    public:
        const char *iid() const override {
            return "srt.driver.InferenceDriver";
        }

        const char *key() const override {
            return "onnx";
        }

        srt::core::NO<srt::driver::InferenceDriver> create() override {
            return srt::core::NO<OnnxDriver>::create();
        }
    };

}

SRT_EXPORT_PLUGIN(srt::driver::onnx::OnnxDriverPlugin)
