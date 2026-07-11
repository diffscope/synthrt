#ifndef SYNTHRT_PLUGIN_EXTRACT_GAME_GAMEPLUGIN_H
#define SYNTHRT_PLUGIN_EXTRACT_GAME_GAMEPLUGIN_H

#include <synthrt/Extract/MidiExtractorPlugin.h>

class GamePlugin : public srt::extract::MidiExtractorPlugin {
public:
    GamePlugin() = default;

    const char *key() const override { return "game"; }

    srt::core::Expected<srt::core::NO<srt::extract::MidiExtractor>>
    createExtractor(srt::core::Runtime *runtime) override;
};

#endif // SYNTHRT_PLUGIN_EXTRACT_GAME_GAMEPLUGIN_H
