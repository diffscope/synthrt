#ifndef SYNTHRT_INFERENCEINTERPRETERPLUGIN_H
#define SYNTHRT_INFERENCEINTERPRETERPLUGIN_H

#include <synthrt/Core/ContribInterpreterPlugin.h>

namespace srt {

    /// The plugin extension point used by the \c inference category.
    class InferenceInterpreterPlugin : public ContribInterpreterPlugin {
    public:
        static constexpr const char *IID = "org.openvpi.InferenceInterpreter";

        ~InferenceInterpreterPlugin() = default;

    protected:
        InferenceInterpreterPlugin() = default;
    };

}

#endif // SYNTHRT_INFERENCEINTERPRETERPLUGIN_H
