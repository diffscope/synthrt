#include "CliRuntime.h"

#include <stdexcept>
#include <utility>

#include <stdcorelib/str.h>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/SingerContrib.h>

namespace ds::cli {

    CliRuntime::CliRuntime(const std::filesystem::path &pluginRoot,
                           Api::Onnx::ExecutionProvider executionProvider, int deviceIndex) {
        // Each contribution category discovers only plugins from its own directory.
        m_synthUnit.setPluginPaths(srt::SingerCategory::NAME,
                                   {pluginRoot / STDC_TSTR("singerproviders")});
        m_synthUnit.setPluginPaths(srt::InferenceCategory::NAME,
                                   {pluginRoot / STDC_TSTR("inferenceinterpreters")});

        // Drivers are runtime services rather than contributions, so they use a separate factory.
        m_driverFactory.addPluginPath(pluginRoot / STDC_TSTR("inferencedrivers"));
        auto driverLoader = m_driverFactory.find(Api::Onnx::API_NAME);
        if (!driverLoader) {
            throw std::runtime_error("failed to find the ONNX inference driver plugin");
        }
        auto driverResult = m_driverFactory.create(driverLoader);
        if (!driverResult) {
            throw std::runtime_error("failed to load inference driver: " +
                                     driverResult.error().message());
        }
        auto onnxDriver = driverResult.take();

        Api::Onnx::DriverInitArgs args;
        args.ep = executionProvider;
        args.deviceIndex = deviceIndex;

        // The default payload is staged directly in the runtime directory and only the CUDA
        // flavor gets its own subdirectory (see ONNX_RUNTIME_DIR / CUDA_RUNTIME_SUBDIR).
        auto runtimeRoot = driverLoader->filePath().parent_path() / STDC_TSTR("runtime");
        args.runtimePath = executionProvider == Api::Onnx::ExecutionProvider::CUDA
                               ? runtimeRoot / STDC_TSTR("cuda")
                               : runtimeRoot;

        if (auto result = onnxDriver->initialize(args); !result) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize ONNX driver: %1)", result.error().message()));
        }

        // SynthUnit owns the initialized driver while loaded contributions can use it.
        if (auto result = m_synthUnit.addRuntimeService(std::move(onnxDriver)); !result) {
            throw std::runtime_error("failed to register inference driver: " +
                                     result.error().message());
        }
    }

}
