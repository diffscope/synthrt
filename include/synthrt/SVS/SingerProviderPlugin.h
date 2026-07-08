#pragma once

#include <synthrt/SVS/SingerProvider.h>
#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/SVS/srt_svs_global.h>

namespace srt::svs {

    class SRT_SVS_EXPORT SingerProviderPlugin : public core::Plugin {
    public:
        ~SingerProviderPlugin() override;
        virtual core::NO<SingerProvider> create(const SingerSpec *spec) = 0;
    };

}
