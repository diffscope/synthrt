#ifndef SYNTHRT_INFERENCEINTERPRETERPLUGIN_H
#define SYNTHRT_INFERENCEINTERPRETERPLUGIN_H

#include <synthrt/Plugin/Plugin.h>
#include <synthrt/SVS/InferenceInterpreter.h>

namespace srt {

    class InferenceInterpreterPlugin : public Plugin {
    public:
        InferenceInterpreterPlugin() = default;
        ~InferenceInterpreterPlugin() = default;

        static constexpr const char *IID = "org.openvpi.InferenceInterpreter";

        const char *iid() const override {
            return IID;
        }

    public:
        /// Builds an interpreter and hands over the only reference to it.
        ///
        /// \note Unique rather than shared, so the caller decides whether it ends up with one
        ///       owner or several. A factory has no business making that call for it.
        virtual UNO<InferenceInterpreter> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(InferenceInterpreterPlugin)
    };

}

#endif // SYNTHRT_INFERENCEINTERPRETERPLUGIN_H
