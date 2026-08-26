#ifndef DSINFER_TOOLS_CLI_CLIRUNTIME_H
#define DSINFER_TOOLS_CLI_CLIRUNTIME_H

#include <filesystem>

#include <synthrt/Core/SynthUnit.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>

namespace ds::cli {

    /// Owns the runtime objects required by one CLI invocation.
    class CliRuntime {
    public:
        CliRuntime(const std::filesystem::path &pluginRoot,
                   Api::Onnx::ExecutionProvider executionProvider, int deviceIndex);

        /// Returns the configured SynthRT runtime.
        srt::SynthUnit &synthUnit() noexcept {
            return m_synthUnit;
        }

    private:
        // The SynthUnit is destroyed first so no runtime object outlives its plugin code.
        InferenceDriverFactory m_driverFactory;
        srt::SynthUnit m_synthUnit;
    };

}

#endif // DSINFER_TOOLS_CLI_CLIRUNTIME_H
