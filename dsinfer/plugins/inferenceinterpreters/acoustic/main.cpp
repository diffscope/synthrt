#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include "AcousticInterpreter.h"

namespace ds {

    class AcousticInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        AcousticInterpreterPlugin() = default;

        const char *key() const {
            return "ai.svs.AcousticInference";
        }

        srt::InferenceInterpreter *create() {
            return new AcousticInterpreter();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::AcousticInterpreterPlugin)