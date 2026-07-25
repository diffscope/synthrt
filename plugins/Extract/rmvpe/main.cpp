#include <synthrt/Extract/PitchExtractorPlugin.h>

#include "RmvpeExtractor.h"

namespace srt::extract::plugins::Rmvpe {

    class RmvpePlugin : public srt::extract::PitchExtractorPlugin {
    public:
        RmvpePlugin() = default;

    public:
        const char *iid() const override { return staticIid(); }
        const char *key() const override { return "rmvpe"; }

        srt::core::Expected<srt::core::NO<srt::extract::PitchExtractor>>
            createExtractor(srt::core::Runtime *runtime) override {
            return srt::core::NO<RmvpeExtractor>::create(runtime);
        }
    };

}

SRT_EXPORT_PLUGIN(srt::extract::plugins::Rmvpe::RmvpePlugin)
