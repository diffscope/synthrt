#include <synthrt/SVS/SingerProviderPlugin.h>

#include "DiffSingerProvider.h"

namespace ds {

    class DiffSingerProviderPlugin : public srt::SingerProviderPlugin {
    public:
        DiffSingerProviderPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>> create() override {
            return std::unique_ptr<srt::ContribInterpreter>(new DiffSingerProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::DiffSingerProviderPlugin)
