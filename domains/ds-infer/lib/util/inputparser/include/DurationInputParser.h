#pragma once

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/JSON.h>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

namespace ds::infer {
    srt::core::Expected<srt::core::NO<srt::svs::Api::Duration::L1::DurationStartInput>>
        parseDurationStartInput(const srt::core::JsonObject &obj);
}
