#ifndef DSINFER_ACOUSTICINPUTPARSER_H
#define DSINFER_ACOUSTICINPUTPARSER_H

#include <memory>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

namespace ds {

    /// Parses an Acoustic level 1 Task input from a obj.
    srt::Expected<std::unique_ptr<Api::Acoustic::L1::AcousticStartInput>>
        parseAcousticStartInput(const srt::JsonObject &obj);

}

#endif // DSINFER_ACOUSTICINPUTPARSER_H
