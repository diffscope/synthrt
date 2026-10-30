#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

#include "DurationInterpreter.h"

namespace ds {

    class DurationInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        DurationInterpreterPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            namespace Dur = Api::Duration::L1;
            if (triplet !=
                srt::ContribTriplet(Dur::API_INTERFACE, Dur::API_LEVEL, Dur::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new DurationInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::DurationInterpreterPlugin)
