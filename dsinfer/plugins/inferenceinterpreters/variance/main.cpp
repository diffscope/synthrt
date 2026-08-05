#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "VarianceInterpreter.h"

namespace ds {

    class VarianceInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        VarianceInterpreterPlugin() = default;

        const char *key() const override {
            return "ai.svs.VarianceInference";
        }

        srt::UNO<srt::InferenceInterpreter> create() override {
            return srt::UNO<VarianceInterpreter>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::VarianceInterpreterPlugin)