#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

#include "VarianceInterpreter.h"

namespace ds {

    class VarianceInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != srt::ContribContractTriplet(Api::Variance::L1::API_INTERFACE,
                                                       Api::Variance::L1::API_LEVEL,
                                                       Api::Variance::L1::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new VarianceInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::VarianceInterpreterPlugin)
