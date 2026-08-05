#ifndef DSINFER_INFERENCEDRIVERPLUGIN_H
#define DSINFER_INFERENCEDRIVERPLUGIN_H

#include <synthrt/Plugin/Plugin.h>
#include <dsinfer/Inference/InferenceDriver.h>

namespace ds {

    class InferenceDriverPlugin : public srt::Plugin {
    public:
        InferenceDriverPlugin() = default;
        ~InferenceDriverPlugin() = default;

        static constexpr const char *IID = "org.openvpi.InferenceDriver";

        const char *iid() const override {
            return IID;
        }

    public:
        /// Builds a driver and hands over the only reference to it.
        ///
        /// \note Unique rather than shared, so the caller decides whether the driver ends up with
        ///       one owner or several. A factory has no business making that call for it.
        virtual srt::UNO<InferenceDriver> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(InferenceDriverPlugin)
    };

}

#endif // DSINFER_INFERENCEDRIVERPLUGIN_H