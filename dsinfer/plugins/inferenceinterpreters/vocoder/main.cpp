#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "VocoderInterpreter.h"

namespace ds {

    class VocoderInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        VocoderInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>> create() override {
            return std::unique_ptr<srt::ContribInterpreter>(new VocoderInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::VocoderInterpreterPlugin)