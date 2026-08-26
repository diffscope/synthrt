#include "CliRuntime.h"

#include <stdexcept>
#include <utility>

#include <stdcorelib/str.h>

namespace ds::cli {

    CliRuntime::CliRuntime(const std::filesystem::path &pluginRoot,
                           Api::Onnx::ExecutionProvider executionProvider, int deviceIndex) {
        // Each contribution category discovers only plugins from its own directory.
        m_synthUnit.setPluginPaths("singer", {pluginRoot / STDC_TSTR("singerproviders")});
        m_synthUnit.setPluginPaths("inference", {pluginRoot / STDC_TSTR("inferenceinterpreters")});

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
        auto runtimeRoot =
            driverLoader->filePath().parent_path() / STDC_TSTR("runtimes") / STDC_TSTR("onnx");
        args.runtimePath = runtimeRoot / (executionProvider == Api::Onnx::ExecutionProvider::CUDA
                                              ? STDC_TSTR("cuda")
                                              : STDC_TSTR("default"));

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
