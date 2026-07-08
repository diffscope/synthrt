#include "VarianceInterpreter.h"
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

namespace srt::svs {

    class VarianceInterpreterPlugin : public InferenceInterpreterPlugin {
    public:
        VarianceInterpreterPlugin() = default;

        const char *iid() const override {
            return "srt.svs.interpreter.variance";
        }

        const char *key() const override {
            return "ai.svs.VarianceInference";
        }

        srt::core::NO<InferenceInterpreter> create() override {
            return srt::core::NO<VarianceInterpreter>::create();
        }
    };

}

SRT_EXPORT_PLUGIN(srt::svs::VarianceInterpreterPlugin)
