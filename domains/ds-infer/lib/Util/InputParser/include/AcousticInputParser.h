#ifndef DSINFER_ACOUSTICINPUTPARSER_H
#define DSINFER_ACOUSTICINPUTPARSER_H

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/JSON.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

namespace ds::infer {
    srt::core::Expected<srt::core::NO<srt::svs::Api::Acoustic::L1::AcousticStartInput>>
        parseAcousticStartInput(const srt::core::JsonObject &obj);
}

#endif // DSINFER_ACOUSTICINPUTPARSER_H