#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "VarianceInterpreter.h"

namespace ds {

    class VarianceInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        VarianceInterpreterPlugin() = default;

        const char *key() const override {
            return "ai.svs.VarianceInference";
        }

        srt::NO<srt::InferenceInterpreter> create() override {
            return srt::NO<VarianceInterpreter>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::VarianceInterpreterPlugin)