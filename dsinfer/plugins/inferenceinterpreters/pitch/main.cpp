#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "PitchInterpreter.h"

namespace ds {

    class PitchInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        PitchInterpreterPlugin() = default;

        const char *key() const override {
            return "ai.svs.PitchInference";
        }

        srt::UNO<srt::InferenceInterpreter> create() override {
            return srt::UNO<PitchInterpreter>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::PitchInterpreterPlugin)