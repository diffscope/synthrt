#ifndef DSINFER_VARIANCEINPUTPARSER_H
#define DSINFER_VARIANCEINPUTPARSER_H

#include <synthrt/Support/Expected.h>
#include <memory>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

namespace ds {
    srt::Expected<std::unique_ptr<Api::Variance::L1::VarianceStartInput>>
        parseVarianceStartInput(const srt::JsonObject &obj);
}

#endif // DSINFER_VARIANCEINPUTPARSER_H