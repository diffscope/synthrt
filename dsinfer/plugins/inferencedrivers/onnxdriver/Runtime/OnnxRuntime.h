#ifndef DSINFER_ONNXDRIVER_ONNXRUNTIME_H
#define DSINFER_ONNXDRIVER_ONNXRUNTIME_H

#include <filesystem>
#include <memory>

#include <onnxruntime_cxx_api.h>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

namespace stdc {
    class SharedLibrary;
}

namespace ds::onnxdriver {

    /// Owns or borrows one ONNX Runtime API compatible with the process and its shared environment.
    class OnnxRuntime {
    public:
        ~OnnxRuntime();

        /// Loads an ONNX Runtime shared library and acquires its API.
        static srt::Expected<std::shared_ptr<OnnxRuntime>> load(const std::filesystem::path &path);

        /// Acquires an externally owned ONNX Runtime API.
        static srt::Expected<std::shared_ptr<OnnxRuntime>>
            borrow(const Api::Onnx::RuntimeApi &runtimeApi);

        /// Returns the API table used by this runtime.
        const Api::Onnx::RuntimeApi &api() const noexcept;

        /// Returns the loaded shared library path, or an empty path for an external runtime.
        const std::filesystem::path &path() const noexcept;

        /// Returns the environment shared by sessions using this runtime.
        Ort::Env &environment() noexcept;

    private:
        OnnxRuntime();

        srt::Expected<void> initializeLoaded(const std::filesystem::path &path);
        srt::Expected<void> initializeBorrowed(const Api::Onnx::RuntimeApi &runtimeApi);
        srt::Expected<void> initializeEnvironment();

        // Declared before the ORT objects so it is destroyed after them.
        std::unique_ptr<stdc::SharedLibrary> m_library;
        Api::Onnx::RuntimeApi m_api;
        std::filesystem::path m_path;
        Ort::Env m_environment;
    };

}

#endif // DSINFER_ONNXDRIVER_ONNXRUNTIME_H
