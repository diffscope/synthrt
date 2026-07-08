#ifndef SRT_DRIVER_ONNX_EXECUTIONPROVIDER_P_H
#define SRT_DRIVER_ONNX_EXECUTIONPROVIDER_P_H

#include <onnxruntime_cxx_api.h>

namespace srt::driver::onnx {

    bool initCUDA(Ort::SessionOptions &options, int deviceIndex,
                  std::string *errorMessage = nullptr);

    bool initDirectML(Ort::SessionOptions &options, int deviceIndex,
                      std::string *errorMessage = nullptr);

}

#endif // SRT_DRIVER_ONNX_EXECUTIONPROVIDER_P_H
