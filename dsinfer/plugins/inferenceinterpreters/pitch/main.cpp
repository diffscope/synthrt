#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

#include "PitchInterpreter.h"

namespace ds {

    class PitchInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            if (triplet != srt::ContribTriplet(Api::Pitch::L1::API_INTERFACE,
                                               Api::Pitch::L1::API_LEVEL,
                                               Api::Pitch::L1::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new PitchInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::PitchInterpreterPlugin)
