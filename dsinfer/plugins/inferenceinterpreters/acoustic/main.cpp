#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

#include "AcousticInterpreter.h"

namespace ds {

    class AcousticInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        AcousticInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            namespace Ac = Api::Acoustic::L1;
            if (triplet != srt::ContribTriplet(Ac::API_INTERFACE, Ac::API_LEVEL, Ac::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new AcousticInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::AcousticInterpreterPlugin)
