#include "VocoderInterpreter.h"
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

namespace srt::svs {

    class VocoderInterpreterPlugin : public InferenceInterpreterPlugin {
    public:
        VocoderInterpreterPlugin() = default;

        const char *iid() const override {
            return "srt.svs.interpreter.vocoder";
        }

        const char *key() const override {
            return "ai.svs.VocoderInference";
        }

        srt::core::NO<InferenceInterpreter> create() override {
            return srt::core::NO<VocoderInterpreter>::create();
        }
    };

}

SRT_EXPORT_PLUGIN(srt::svs::VocoderInterpreterPlugin)
