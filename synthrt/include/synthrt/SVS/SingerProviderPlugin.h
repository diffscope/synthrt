#ifndef SYNTHRT_SINGERPROVIDERPLUGIN_H
#define SYNTHRT_SINGERPROVIDERPLUGIN_H

#include <synthrt/Plugin/Plugin.h>
#include <synthrt/SVS/SingerProvider.h>

namespace srt {

    class SingerProviderPlugin : public Plugin {
    public:
        SingerProviderPlugin() = default;
        ~SingerProviderPlugin() = default;

        static constexpr const char *IID = "org.openvpi.SingerProvider";

        const char *iid() const override {
            return IID;
        }

    public:
        /// Builds a provider and hands over the only reference to it.
        ///
        /// \note Unique rather than shared, as \c InferenceInterpreterPlugin::create() is.
        virtual UNO<SingerProvider> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(SingerProviderPlugin)
    };

}

#endif // SYNTHRT_SINGERPROVIDERPLUGIN_H
