#ifndef DSINFER_INFERENCEDRIVERPLUGIN_H
#define DSINFER_INFERENCEDRIVERPLUGIN_H

#include <synthrt/Plugin/Plugin.h>
#include <dsinfer/Inference/InferenceDriver.h>

namespace ds {

    class InferenceDriverPlugin : public srt::Plugin {
    public:
        InferenceDriverPlugin() = default;
        ~InferenceDriverPlugin() = default;

        const char *iid() const override {
            return "org.openvpi.InferenceDriver";
        }

    public:
        virtual srt::NO<InferenceDriver> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(InferenceDriverPlugin)
    };

}

#endif // DSINFER_INFERENCEDRIVERPLUGIN_H