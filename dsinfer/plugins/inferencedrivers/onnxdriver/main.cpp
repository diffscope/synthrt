#include <dsinfer/Inference/InferenceDriverPlugin.h>

#include "OnnxDriver.h"

namespace ds {

    class OnnxDriverPlugin : public InferenceDriverPlugin {
    public:
        OnnxDriverPlugin() = default;

        srt::Expected<std::unique_ptr<InferenceDriver>> create() override {
            return std::unique_ptr<InferenceDriver>(new OnnxDriver());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::OnnxDriverPlugin)
