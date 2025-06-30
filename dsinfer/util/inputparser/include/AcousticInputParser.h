#ifndef DSINFER_ACOUSTICINPUTPARSER_H
#define DSINFER_ACOUSTICINPUTPARSER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

namespace ds {
    srt::Expected<srt::NO<Api::Acoustic::L1::AcousticStartInput>>
        parseAcousticStartInput(const srt::JsonObject &obj);
}

#endif // DSINFER_ACOUSTICINPUTPARSER_H