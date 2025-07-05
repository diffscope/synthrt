#ifndef DSINFER_INFERUTIL_DRIVER_H
#define DSINFER_INFERUTIL_DRIVER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/SVS/Inference.h>
#include <dsinfer/Inference/InferenceDriver.h>

namespace ds::inferutil {
    srt::Expected<srt::NO<InferenceDriver>> getInferenceDriver(const srt::Inference *obj);
}

#endif // DSINFER_INFERUTIL_DRIVER_H