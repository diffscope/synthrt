#include <dsinfer/Inference/InferenceDriverPlugin.h>

#include "OnnxDriver.h"

namespace ds {

    class OnnxDriverPlugin : public InferenceDriverPlugin {
    public:
        OnnxDriverPlugin() = default;

    public:
        const char *key() const override {
            return "onnx";
        }

        srt::UNO<InferenceDriver> create() override {
            return srt::UNO<OnnxDriver>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::OnnxDriverPlugin)