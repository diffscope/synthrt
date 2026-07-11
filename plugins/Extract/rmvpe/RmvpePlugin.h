#ifndef RMVPE_PLUGIN_H
#define RMVPE_PLUGIN_H

#include <synthrt/Extract/PitchExtractorPlugin.h>

class RmvpePlugin : public srt::extract::PitchExtractorPlugin {
public:
    RmvpePlugin() = default;

    const char *key() const override { return "rmvpe"; }

    srt::core::Expected<srt::core::NO<srt::extract::PitchExtractor>>
    createExtractor(srt::core::Runtime *runtime) override;
};

#endif // RMVPE_PLUGIN_H
