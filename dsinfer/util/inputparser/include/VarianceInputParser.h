#ifndef DSINFER_VARIANCEINPUTPARSER_H
#define DSINFER_VARIANCEINPUTPARSER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

namespace ds {
    srt::Expected<srt::NO<Api::Variance::L1::VarianceStartInput>>
        parseVarianceStartInput(const srt::JsonObject &obj);
}

#endif // DSINFER_VARIANCEINPUTPARSER_H