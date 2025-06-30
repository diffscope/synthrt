#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "DurationInterpreter.h"

namespace ds {

    class DurationInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        DurationInterpreterPlugin() = default;

        const char *key() const override {
            return "ai.svs.DurationInference";
        }

        srt::NO<srt::InferenceInterpreter> create() override {
            return srt::NO<DurationInterpreter>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::DurationInterpreterPlugin)