#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

#include "PitchInterpreter.h"

namespace ds {

    class PitchInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        PitchInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            namespace Pit = Api::Pitch::L1;
            if (triplet !=
                srt::ContribTriplet(Pit::API_INTERFACE, Pit::API_LEVEL, Pit::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported pitch interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new PitchInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::PitchInterpreterPlugin)
