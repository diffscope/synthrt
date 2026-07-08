#include "PitchInterpreter.h"
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

namespace srt::svs {

    class PitchInterpreterPlugin : public InferenceInterpreterPlugin {
    public:
        PitchInterpreterPlugin() = default;

        const char *iid() const override {
            return "srt.svs.interpreter.pitch";
        }

        const char *key() const override {
            return "ai.svs.PitchInference";
        }

        srt::core::NO<InferenceInterpreter> create() override {
            return srt::core::NO<PitchInterpreter>::create();
        }
    };

}

SRT_EXPORT_PLUGIN(srt::svs::PitchInterpreterPlugin)
