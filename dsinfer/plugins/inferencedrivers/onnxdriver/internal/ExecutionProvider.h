#ifndef DSINFER_ONNXDRIVER_EXECUTIONPROVIDER_P_H
#define DSINFER_ONNXDRIVER_EXECUTIONPROVIDER_P_H

#include <onnxruntime_cxx_api.h>

namespace ds::onnxdriver {

    bool initCUDA(Ort::SessionOptions &options, int deviceIndex,
                  std::string *errorMessage = nullptr);

    bool initDirectML(Ort::SessionOptions &options, int deviceIndex,
                      std::string *errorMessage = nullptr);

}

#endif // DSINFER_ONNXDRIVER_EXECUTIONPROVIDER_P_H
