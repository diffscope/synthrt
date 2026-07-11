#include "GamePlugin.h"

#include "GameExtractor.h"

srt::core::Expected<srt::core::NO<srt::extract::MidiExtractor>>
GamePlugin::createExtractor(srt::core::Runtime *runtime) {
    return srt::core::NO<GameExtractor>::create(runtime);
}
