#ifndef SYNTHRT_INFERENCEINTERPRETERPLUGIN_H
#define SYNTHRT_INFERENCEINTERPRETERPLUGIN_H

#include <synthrt/Plugin/Plugin.h>
#include <synthrt/SVS/InferenceInterpreter.h>

namespace srt {

    class InferenceInterpreterPlugin : public Plugin {
    public:
        InferenceInterpreterPlugin() = default;
        ~InferenceInterpreterPlugin() = default;

        const char *iid() const override {
            return "ai.svs.InferenceInterpreter";
        }

    public:
        virtual NO<InferenceInterpreter> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(InferenceInterpreterPlugin)
    };

}

#endif // SYNTHRT_INFERENCEINTERPRETERPLUGIN_H