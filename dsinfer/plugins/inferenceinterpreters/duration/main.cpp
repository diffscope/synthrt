#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "DurationInterpreter.h"

namespace ds {

    class DurationInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        DurationInterpreterPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>> create() override {
            return std::unique_ptr<srt::ContribInterpreter>(new DurationInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::DurationInterpreterPlugin)
