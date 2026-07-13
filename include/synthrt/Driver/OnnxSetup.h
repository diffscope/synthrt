#pragma once

#include <filesystem>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/Driver/srt_driver_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::driver {

    /// Execution provider configuration for the ONNX inference driver.
    struct SRT_DRIVER_EXPORT OnnxDriverConfig {
        srt::driver::onnx::ExecutionProvider ep =
            srt::driver::onnx::ExecutionProvider::DMLExecutionProvider;
        int deviceIndex = 0;
    };

    /// Setup ONNX inference driver in a Runtime.
    ///
    /// 1. Registers plugin paths for singer-provider, inference driver,
    ///    and 5 inference interpreters under pluginRoot.
    /// 2. Loads the "onnx" InferenceDriverPlugin.
    /// 3. Creates OnnxDriver with the given EP/device and initializes it.
    /// 4. Registers the driver in the "inference" category as "dsdriver".
    ///
    /// This is the replacement for the old DiffSingerSession::initialize()
    /// Stage 5. Call after Runtime is created. Safe to call multiple times
    /// (re-registers plugin paths; the "dsdriver" object is replaced).
    ///
    /// \param runtime     The Runtime to register plugins and driver into.
    /// \param pluginRoot  Root directory containing subdirs:
    ///                     - srt-driver/inferencedrivers/
    ///                     - diffsinger/singerproviders/
    ///                     - diffsinger/inferenceinterpreters/
    /// \param config      ONNX execution provider and device index.
    srt::core::Expected<void> SRT_DRIVER_EXPORT setupOnnxInferenceDriver(
        srt::core::Runtime &runtime,
        const std::filesystem::path &pluginRoot,
        const OnnxDriverConfig &config);

} // namespace srt::driver
