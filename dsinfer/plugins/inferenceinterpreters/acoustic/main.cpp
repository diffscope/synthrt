#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "AcousticInterpreter.h"

namespace ds {

    class AcousticInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        AcousticInterpreterPlugin() = default;

        const char *key() const override {
            return "ai.svs.AcousticInference";
        }

        srt::NO<srt::InferenceInterpreter> create() override {
            return srt::NO<AcousticInterpreter>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::AcousticInterpreterPlugin)