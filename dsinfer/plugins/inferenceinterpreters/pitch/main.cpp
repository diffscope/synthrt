#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "PitchInterpreter.h"

namespace ds {

    class PitchInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        PitchInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>> create() override {
            return std::unique_ptr<srt::ContribInterpreter>(new PitchInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::PitchInterpreterPlugin)