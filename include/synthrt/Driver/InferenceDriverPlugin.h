#pragma once

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Driver/InferenceDriver.h>

namespace srt::driver {

    /// IID constant for InferenceDriverPlugin.
    inline constexpr auto kInferenceDriverPluginIid = "srt.driver.InferenceDriver";

    class SRT_DRIVER_EXPORT InferenceDriverPlugin : public srt::core::Plugin {
    public:
        InferenceDriverPlugin();
        ~InferenceDriverPlugin() override;

        const char *iid() const override { return staticIid(); }

        /// Static IID accessor — allows PluginFactory::plugin<T>(key) to
        /// obtain the IID without invoking the virtual method on a null
        /// pointer (UB).
        static const char *staticIid() { return kInferenceDriverPluginIid; }

    public:
        virtual srt::core::NO<InferenceDriver> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(InferenceDriverPlugin)
    };

}
