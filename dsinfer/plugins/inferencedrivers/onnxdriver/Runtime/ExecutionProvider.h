#ifndef DSINFER_ONNXDRIVER_EXECUTIONPROVIDER_H
#define DSINFER_ONNXDRIVER_EXECUTIONPROVIDER_H

#include <onnxruntime_cxx_api.h>

#include <synthrt/Support/Expected.h>

namespace ds::onnxdriver {

    /// Appends the CUDA execution provider to \a options.
    srt::Expected<void> appendCudaExecutionProvider(Ort::SessionOptions &options, int deviceIndex);

    /// Appends the DirectML execution provider to \a options.
    srt::Expected<void> appendDirectMLExecutionProvider(Ort::SessionOptions &options,
                                                        int deviceIndex);

}

#endif // DSINFER_ONNXDRIVER_EXECUTIONPROVIDER_H
