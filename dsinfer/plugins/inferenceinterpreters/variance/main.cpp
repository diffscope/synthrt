#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

#include "VarianceInterpreter.h"

namespace ds {

    class VarianceInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        VarianceInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            namespace Var = Api::Variance::L1;
            if (triplet !=
                srt::ContribTriplet(Var::API_INTERFACE, Var::API_LEVEL, Var::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new VarianceInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::VarianceInterpreterPlugin)
