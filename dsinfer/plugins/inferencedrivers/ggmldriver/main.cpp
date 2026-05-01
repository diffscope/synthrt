#include <dsinfer/Inference/InferenceDriverPlugin.h>

#include "GgmlDriver.h"

namespace ds {

    class GgmlDriverPlugin : public InferenceDriverPlugin {
    public:
        GgmlDriverPlugin() = default;

    public:
        const char *key() const override {
            return "ggml";
        }

        srt::NO<InferenceDriver> create() override {
            return srt::NO<GgmlDriver>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::GgmlDriverPlugin)
