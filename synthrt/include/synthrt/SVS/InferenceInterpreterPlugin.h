#ifndef SYNTHRT_SVS_INFERENCEINTERPRETERPLUGIN_H
#define SYNTHRT_SVS_INFERENCEINTERPRETERPLUGIN_H

#include <synthrt/Core/ContribInterpreterPlugin.h>

namespace srt {

    /// The plugin extension point used by the \c inference category.
    class InferenceInterpreterPlugin : public ContribInterpreterPlugin {
    public:
        static constexpr const char *IID = "org.openvpi.synthrt.plugin.InferenceInterpreter";

        ~InferenceInterpreterPlugin() = default;

    protected:
        InferenceInterpreterPlugin() = default;
    };

}

#endif // SYNTHRT_SVS_INFERENCEINTERPRETERPLUGIN_H
