#include <synthrt/Extract/MidiExtractorPlugin.h>

#include "GameExtractor.h"

namespace srt::extract::plugins::Game {

    class GamePlugin : public srt::extract::MidiExtractorPlugin {
    public:
        GamePlugin() = default;

    public:
        const char *iid() const override { return staticIid(); }
        const char *key() const override { return "game"; }

        srt::core::Expected<srt::core::NO<srt::extract::MidiExtractor>>
            createExtractor(srt::core::Runtime *runtime) override {
            return srt::core::NO<GameExtractor>::create(runtime);
        }
    };

}

SRT_EXPORT_PLUGIN(srt::extract::plugins::Game::GamePlugin)
