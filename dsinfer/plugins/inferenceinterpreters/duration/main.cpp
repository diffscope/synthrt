#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

#include "DurationInterpreter.h"

namespace ds {

    class DurationInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        DurationInterpreterPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace Dur = Api::Duration::L1;
            if (interfaceName != Dur::API_INTERFACE || level != Dur::API_LEVEL ||
                variant != Dur::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported duration interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new DurationInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::DurationInterpreterPlugin)
