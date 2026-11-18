#ifndef DSINFER_TESTS_MANUAL_RUNNER_RUNNERRUNTIME_H
#define DSINFER_TESTS_MANUAL_RUNNER_RUNNERRUNTIME_H

#include <filesystem>
#include <memory>

#include <stdcorelib/support/sharedlibrary.h>

#include <synthrt/Core/SynthUnit.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>

namespace ds::manual {

    /// Owns the runtime objects required by one manual runner invocation.
    class RunnerRuntime {
    public:
        RunnerRuntime(const std::filesystem::path &pluginRoot,
                      const std::filesystem::path &wolfPluginRoot,
                      const std::filesystem::path &wolfLibrary,
                      const std::filesystem::path &onnxRuntime,
                      Api::Onnx::ExecutionProvider executionProvider, int deviceIndex);

        /// Returns the configured SynthRT runtime.
        srt::SynthUnit &synthUnit() noexcept {
            return *m_synthUnit;
        }

    private:
        // The SynthUnit is destroyed first so no runtime object outlives its plugin code. The wolf
        // library is loaded before the SynthUnit is constructed, because its static initializer
        // registers the linguist category.
        stdc::SharedLibrary m_wolfLibrary;
        InferenceDriverFactory m_driverFactory;
        std::unique_ptr<srt::SynthUnit> m_synthUnit;
    };

}

#endif // DSINFER_TESTS_MANUAL_RUNNER_RUNNERRUNTIME_H
