#pragma once

#include <onnxruntime_cxx_api.h>

namespace srt::driver::onnx {

    bool initCUDA(Ort::SessionOptions &options, int deviceIndex,
                  std::string *errorMessage = nullptr);

    bool initDirectML(Ort::SessionOptions &options, int deviceIndex,
                      std::string *errorMessage = nullptr);

}
