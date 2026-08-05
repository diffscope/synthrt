#include <synthrt/SVS/SingerProviderPlugin.h>

#include "DiffSingerProvider.h"

namespace ds {

    class DiffSingerProviderPlugin : public srt::SingerProviderPlugin {
    public:
        DiffSingerProviderPlugin() = default;

        const char *key() const {
            return "diffsinger";
        }

        srt::UNO<srt::SingerProvider> create() override {
            return srt::UNO<DiffSingerProvider>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::DiffSingerProviderPlugin)