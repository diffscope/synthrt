#ifndef DSINFER_DURATIONINPUTPARSER_H
#define DSINFER_DURATIONINPUTPARSER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

namespace ds {
    srt::Expected<srt::NO<Api::Duration::L1::DurationStartInput>>
        parseDurationStartInput(const srt::JsonObject &obj);
}

#endif // DSINFER_DURATIONINPUTPARSER_H