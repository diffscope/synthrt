#ifndef DSINFER_PITCHINPUTPARSER_H
#define DSINFER_PITCHINPUTPARSER_H

#include <memory>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

namespace ds {

    /// Parses a Pitch level 1 Task input from a obj.
    srt::Expected<std::unique_ptr<Api::Pitch::L1::PitchStartInput>>
        parsePitchStartInput(const srt::JsonObject &obj);

}

#endif // DSINFER_PITCHINPUTPARSER_H
