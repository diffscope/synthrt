#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "AcousticInterpreter.h"

namespace ds {

    class AcousticInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        AcousticInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>> create() override {
            return std::unique_ptr<srt::ContribInterpreter>(new AcousticInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::AcousticInterpreterPlugin)
