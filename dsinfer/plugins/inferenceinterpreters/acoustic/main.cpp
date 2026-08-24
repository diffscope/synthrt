#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

#include "AcousticInterpreter.h"

namespace ds {

    class AcousticInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        AcousticInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace Ac = Api::Acoustic::L1;
            if (interfaceName != Ac::API_INTERFACE || level != Ac::API_LEVEL ||
                variant != Ac::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported acoustic interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new AcousticInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::AcousticInterpreterPlugin)
