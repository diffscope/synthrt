#ifndef DSINFER_PITCHINPUTPARSER_H
#define DSINFER_PITCHINPUTPARSER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

namespace ds {
    srt::Expected<srt::NO<Api::Pitch::L1::PitchStartInput>>
        parsePitchStartInput(const srt::JsonObject &obj);
}

#endif // DSINFER_PITCHINPUTPARSER_H