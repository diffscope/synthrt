#ifndef DSINFER_DURATIONINPUTPARSER_H
#define DSINFER_DURATIONINPUTPARSER_H

#include <memory>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

namespace ds {

    /// Parses a Duration level 1 Task input from a obj.
    srt::Expected<std::unique_ptr<Api::Duration::L1::DurationStartInput>>
        parseDurationStartInput(const srt::JsonObject &obj);

}

#endif // DSINFER_DURATIONINPUTPARSER_H
