#ifndef SRT_DRIVER_INFERENCEDRIVERPLUGIN_H
#define SRT_DRIVER_INFERENCEDRIVERPLUGIN_H

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Driver/InferenceDriver.h>

namespace srt::driver {

    class SRT_DRIVER_EXPORT InferenceDriverPlugin : public srt::core::Plugin {
    public:
        InferenceDriverPlugin();
        ~InferenceDriverPlugin() override;

        const char *iid() const override;

    public:
        virtual srt::core::NO<InferenceDriver> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(InferenceDriverPlugin)
    };

}

#endif // SRT_DRIVER_INFERENCEDRIVERPLUGIN_H
