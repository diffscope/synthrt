#include "RmvpePlugin.h"

#include "RmvpeExtractor.h"

srt::core::Expected<srt::core::NO<srt::extract::PitchExtractor>>
RmvpePlugin::createExtractor(srt::core::Runtime *runtime) {
    return srt::core::NO<RmvpeExtractor>::create(runtime);
}
