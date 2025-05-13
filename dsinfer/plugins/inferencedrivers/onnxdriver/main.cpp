#include <dsinfer/Inference/InferenceDriverPlugin.h>

#include "onnxdriver.h"

namespace ds {

    class OnnxDriverPlugin : public InferenceDriverPlugin {
    public:
        OnnxDriverPlugin() = default;

    public:
        const char *key() const override {
            return "onnx";
        }

    public:
        srt::NO<InferenceDriver> create() override {
            return srt::NO<OnnxDriver>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::OnnxDriverPlugin)