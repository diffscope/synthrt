#ifndef DSINFER_INTERPRETER_COMMON_DRIVER_H
#define DSINFER_INTERPRETER_COMMON_DRIVER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/SVS/Inference.h>
#include <dsinfer/Inference/InferenceDriver.h>

namespace ds::InterpreterCommon {
    srt::Expected<srt::NO<InferenceDriver>> getInferenceDriver(const srt::Inference *obj);
}

#endif // DSINFER_INTERPRETER_COMMON_DRIVER_H