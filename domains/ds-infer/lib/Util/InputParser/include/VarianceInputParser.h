#ifndef DSINFER_VARIANCEINPUTPARSER_H
#define DSINFER_VARIANCEINPUTPARSER_H

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/JSON.h>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

namespace ds::infer {
    srt::core::Expected<srt::core::NO<srt::svs::Api::Variance::L1::VarianceStartInput>>
        parseVarianceStartInput(const srt::core::JsonObject &obj);
}

#endif // DSINFER_VARIANCEINPUTPARSER_H