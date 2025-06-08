#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "VocoderInterpreter.h"

namespace ds {

    class VocoderInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        VocoderInterpreterPlugin() = default;

        const char *key() const override {
            return "ai.svs.VocoderInference";
        }

        srt::NO<srt::InferenceInterpreter> create() override {
            return srt::NO<VocoderInterpreter>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::VocoderInterpreterPlugin)