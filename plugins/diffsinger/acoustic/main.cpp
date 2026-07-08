#include "AcousticInterpreter.h"
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

namespace srt::svs {

    class AcousticInterpreterPlugin : public InferenceInterpreterPlugin {
    public:
        AcousticInterpreterPlugin() = default;

        const char *iid() const override {
            return "srt.svs.interpreter.acoustic";
        }

        const char *key() const override {
            return "ai.svs.AcousticInference";
        }

        srt::core::NO<InferenceInterpreter> create() override {
            return srt::core::NO<AcousticInterpreter>::create();
        }
    };

}

SRT_EXPORT_PLUGIN(srt::svs::AcousticInterpreterPlugin)
