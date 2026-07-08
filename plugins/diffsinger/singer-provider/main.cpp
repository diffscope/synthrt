#include "DiffSingerProvider.h"
#include <synthrt/SVS/SingerProviderPlugin.h>

namespace srt::svs {

    class DiffSingerProviderPlugin : public SingerProviderPlugin {
    public:
        DiffSingerProviderPlugin() = default;

        const char *iid() const override {
            return "srt.svs.singer-provider.diffsinger";
        }

        const char *key() const override {
            return "diffsinger";
        }

        core::NO<SingerProvider> create(const SingerSpec *spec) override {
            return core::NO<DiffSingerProvider>::create(spec);
        }
    };

}

SRT_EXPORT_PLUGIN(srt::svs::DiffSingerProviderPlugin)
