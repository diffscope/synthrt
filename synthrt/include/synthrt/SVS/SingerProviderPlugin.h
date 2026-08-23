#ifndef SYNTHRT_SINGERPROVIDERPLUGIN_H
#define SYNTHRT_SINGERPROVIDERPLUGIN_H

#include <synthrt/Core/ContribInterpreterPlugin.h>

namespace srt {

    /// The plugin extension point used by the \c singer category.
    class SingerProviderPlugin : public ContribInterpreterPlugin {
    public:
        static constexpr const char *IID = "org.openvpi.SingerProvider";

        ~SingerProviderPlugin() override = default;

    protected:
        SingerProviderPlugin() = default;
    };

}

#endif // SYNTHRT_SINGERPROVIDERPLUGIN_H
