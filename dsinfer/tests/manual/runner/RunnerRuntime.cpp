#include "RunnerRuntime.h"

#include <stdexcept>
#include <utility>
#include <vector>

#include <stdcorelib/str.h>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/SingerContrib.h>

namespace ds::manual {

    namespace {

        void loadWolfLibrary(stdc::SharedLibrary &library, const std::filesystem::path &path) {
            if (path.empty()) {
                return;
            }

            const auto hints = stdc::SharedLibrary::ResolveAllSymbolsHint |
                               stdc::SharedLibrary::ExportExternalSymbolsHint |
                               stdc::SharedLibrary::SearchLibraryLoadDirectoryHint |
                               stdc::SharedLibrary::PreventUnloadHint;
            if (!library.open(path, hints)) {
                throw std::runtime_error("failed to load wolf library: " + library.errorMessage());
            }
        }

    }

    RunnerRuntime::RunnerRuntime(const std::filesystem::path &pluginRoot,
                                 const std::filesystem::path &wolfPluginRoot,
                                 const std::filesystem::path &wolfLibrary,
                                 const std::filesystem::path &onnxRuntime,
                                 Api::Onnx::ExecutionProvider executionProvider, int deviceIndex) {
        loadWolfLibrary(m_wolfLibrary, wolfLibrary);
        if (wolfPluginRoot.empty() != wolfLibrary.empty()) {
            throw std::runtime_error(
                "the wolf plugin root and wolf library must be provided together");
        }

        m_synthUnit = std::make_unique<srt::SynthUnit>();

        // Each contribution category discovers only plugins from its own directory.
        m_synthUnit->setPluginPaths(srt::SingerCategory::NAME,
                                    {pluginRoot / STDC_TSTR("singerproviders")});
        std::vector<std::filesystem::path> inferencePluginPaths = {
            pluginRoot / STDC_TSTR("inferenceinterpreters")};
        if (!wolfPluginRoot.empty()) {
            inferencePluginPaths.push_back(wolfPluginRoot / STDC_TSTR("inferenceinterpreters"));
        }
        m_synthUnit->setPluginPaths(srt::InferenceCategory::NAME, inferencePluginPaths);
        if (!wolfPluginRoot.empty()) {
            m_synthUnit->setPluginPaths("linguist",
                                        {wolfPluginRoot / STDC_TSTR("linguistproviders")});
        }

        // Drivers are runtime services rather than contributions, so they use a separate factory.
        m_driverFactory.addPluginPath(pluginRoot / STDC_TSTR("inferencedrivers"));
        auto driverLoader = m_driverFactory.find(Api::Onnx::Name);
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
        if (onnxRuntime.empty()) {
            auto runtimeRoot = driverLoader->filePath().parent_path() / STDC_TSTR("runtime");
            args.runtimePath = executionProvider == Api::Onnx::ExecutionProvider::CUDA
                                   ? runtimeRoot / STDC_TSTR("cuda")
                                   : runtimeRoot;
        } else {
            args.runtimePath = onnxRuntime;
        }

        if (auto result = onnxDriver->initialize(args); !result) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize ONNX driver: %1)", result.error().message()));
        }

        // SynthUnit owns the initialized driver while loaded contributions can use it.
        if (auto result = m_synthUnit->addRuntimeService(std::move(onnxDriver)); !result) {
            throw std::runtime_error("failed to register inference driver: " +
                                     result.error().message());
        }
    }

}
