#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

#include "VarianceInterpreter.h"

namespace ds {

    class VarianceInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        VarianceInterpreterPlugin() = default;


        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace Var = Api::Variance::L1;
            if (interfaceName != Var::API_INTERFACE || level != Var::API_LEVEL ||
                variant != Var::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported variance interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new VarianceInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::VarianceInterpreterPlugin)
