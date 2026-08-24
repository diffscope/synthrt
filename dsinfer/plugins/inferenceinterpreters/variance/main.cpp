#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "VarianceInterpreter.h"

namespace ds {

    class VarianceInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        VarianceInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>> create() override {
            return std::unique_ptr<srt::ContribInterpreter>(new VarianceInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::VarianceInterpreterPlugin)