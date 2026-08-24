#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

#include "PitchInterpreter.h"

namespace ds {

    class PitchInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        PitchInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace Pit = Api::Pitch::L1;
            if (interfaceName != Pit::API_INTERFACE || level != Pit::API_LEVEL ||
                variant != Pit::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported pitch interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new PitchInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::PitchInterpreterPlugin)
