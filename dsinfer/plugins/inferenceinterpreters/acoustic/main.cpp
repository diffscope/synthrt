#include <synthrt/SVS/InferenceInterpreterPlugin.h>

namespace ds {

    class AcousticInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        AcousticInterpreterPlugin() = default;

        const char *key() const {
            return "ai.svs.AcousticInference";
        }

        srt::InferenceInterpreter *create() {
            return nullptr;
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(ds::AcousticInterpreterPlugin)