#ifndef DSINFER_INFERUTIL_DRIVER_H
#define DSINFER_INFERUTIL_DRIVER_H

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/Driver/InferenceDriver.h>

namespace ds::infer::inferutil {
    srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>> getInferenceDriver(const srt::svs::Inference *obj);
}

#endif // DSINFER_INFERUTIL_DRIVER_H