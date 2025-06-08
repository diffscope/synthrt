#ifndef SYNTHRT_SINGERPROVIDERPLUGIN_H
#define SYNTHRT_SINGERPROVIDERPLUGIN_H

#include <synthrt/Plugin/Plugin.h>
#include <synthrt/SVS/SingerProvider.h>

namespace srt {

    class SingerProviderPlugin : public Plugin {
    public:
        SingerProviderPlugin() = default;
        ~SingerProviderPlugin() = default;

        const char *iid() const override {
            return "org.openvpi.SingerProvider";
        }

    public:
        virtual NO<SingerProvider> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(SingerProviderPlugin)
    };

}

#endif // SYNTHRT_SINGERPROVIDERPLUGIN_H