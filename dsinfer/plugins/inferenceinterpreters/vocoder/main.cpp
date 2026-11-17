#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include "VocoderInterpreter.h"

namespace ds {

    class VocoderInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != srt::ContribContractTriplet(Api::Vocoder::L1::API_INTERFACE,
                                                       Api::Vocoder::L1::API_LEVEL,
                                                       Api::Vocoder::L1::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new VocoderInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::VocoderInterpreterPlugin)
