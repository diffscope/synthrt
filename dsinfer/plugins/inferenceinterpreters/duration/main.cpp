#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "DurationInterpreter.h"

namespace ds {

    class DurationInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        DurationInterpreterPlugin() = default;

        const char *key() const override {
            return "ai.svs.DurationInference";
        }

        srt::UNO<srt::InferenceInterpreter> create() override {
            return srt::UNO<DurationInterpreter>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::DurationInterpreterPlugin)