#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include "VocoderInterpreter.h"

namespace ds {

    class VocoderInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        VocoderInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace Vo = Api::Vocoder::L1;
            if (interfaceName != Vo::API_INTERFACE || level != Vo::API_LEVEL ||
                variant != Vo::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported vocoder interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new VocoderInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::VocoderInterpreterPlugin)
