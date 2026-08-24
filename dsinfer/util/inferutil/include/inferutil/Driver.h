#ifndef DSINFER_INFERUTIL_DRIVER_H
#define DSINFER_INFERUTIL_DRIVER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Inference/InferenceDriver.h>

namespace ds::inferutil {

    /// Returns the driver registered on the inference category with the expected backend.
    ///
    /// \note Borrowed, not owned. The category owns the driver and outlives every inference that
    ///       runs against it.
    srt::Expected<InferenceDriver *> getInferenceDriver(const srt::Inference *obj);

}

#endif // DSINFER_INFERUTIL_DRIVER_H
