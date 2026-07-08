#include "DurationInterpreter.h"
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

namespace srt::svs {

    class DurationInterpreterPlugin : public InferenceInterpreterPlugin {
    public:
        DurationInterpreterPlugin() = default;

        const char *iid() const override {
            return "srt.svs.interpreter.duration";
        }

        const char *key() const override {
            return "ai.svs.DurationInference";
        }

        srt::core::NO<InferenceInterpreter> create() override {
            return srt::core::NO<DurationInterpreter>::create();
        }
    };

}

SRT_EXPORT_PLUGIN(srt::svs::DurationInterpreterPlugin)
