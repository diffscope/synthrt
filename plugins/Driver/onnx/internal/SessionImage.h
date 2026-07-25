#pragma once

#include <atomic>
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

        // BUG-DRIVER-01: Tracks in-flight Ort::RunAsync calls so that
        // Session::close() can refuse to delete this image while ORT is still
        // using session. Incremented before RunAsync, decremented in
        // runAsyncCallback. Simplified scheme: close() returns an error when
        // this is non-zero instead of waiting (callers must wait for async
        // completion before closing).
        std::atomic<int> inFlightCount{0};
    };

}
