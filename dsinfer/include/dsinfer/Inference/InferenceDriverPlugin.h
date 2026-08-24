#ifndef DSINFER_INFERENCEDRIVERPLUGIN_H
#define DSINFER_INFERENCEDRIVERPLUGIN_H

#include <memory>

#include <stdcorelib/plugin/plugin.h>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/dsinfer_global.h>

namespace ds {

    /// A plugin factory that creates inference drivers.
    class DSINFER_EXPORT InferenceDriverPlugin : public stdc::plugin::Plugin {
    public:
        static constexpr const char *IID = InferenceDriver::IID;

        virtual ~InferenceDriverPlugin() = default;

        /// Creates a driver implemented by this plugin.
        virtual srt::Expected<std::unique_ptr<InferenceDriver>> create() = 0;

    protected:
        InferenceDriverPlugin() = default;
    };

}

#endif // DSINFER_INFERENCEDRIVERPLUGIN_H
