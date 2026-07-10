#ifndef SRT_DRIVER_ONNX_SESSIONIMAGE_P_H
#define SRT_DRIVER_ONNX_SESSIONIMAGE_P_H

#include <filesystem>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>

#include <onnxruntime_cxx_api.h>

namespace srt::driver::onnx {

    class SessionImage {
    public:
        SessionImage();
        ~SessionImage();

        bool open(const std::filesystem::path &onnxPath, int hints,
                  ExecutionProvider ep, int deviceIndex,
                  std::string *errorMessage = nullptr);

    public:
        std::vector<std::string> inputNames;
        std::vector<std::string> outputNames;

        Ort::Env env;
        Ort::Session session;
    };

}

#endif // SRT_DRIVER_ONNX_SESSIONIMAGE_P_H
