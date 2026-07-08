#ifndef DSINFER_PITCHINPUTPARSER_H
#define DSINFER_PITCHINPUTPARSER_H

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/JSON.h>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

namespace ds::infer {
    srt::core::Expected<srt::core::NO<srt::svs::Api::Pitch::L1::PitchStartInput>>
        parsePitchStartInput(const srt::core::JsonObject &obj);
}

#endif // DSINFER_PITCHINPUTPARSER_H