#pragma once

#include <filesystem>
#include <vector>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::g2p {

    /// Setup the G2P ONNX driver by reusing the Runtime's inference ONNX driver
    /// (registered as "dsdriver" in the "inference" category by
    /// srt::driver::setupOnnxInferenceDriver).
    ///
    /// This is the G2P-side counterpart of setupOnnxInferenceDriver. It:
    ///   1. Registers G2P plugin search paths (task + driver IIDs) on the
    ///      process-level srt::g2p::Manager via addPluginPath().
    ///   2. Locates the inference "dsdriver" object in the Runtime's "inference"
    ///      category and casts it to srt::driver::InferenceDriver.
    ///   3. Wraps it with a CPU-only SessionFactory adapter that forces
    ///      useCpu=true on every session open() (G2P must not compete with
    ///      GPU inference).
    ///   4. Registers the adapter in the Manager's kDriverCategory under the
    ///      name kG2pOnnxDriverName ("g2pOnnxDriver").
    ///
    /// Prerequisite: srt::driver::setupOnnxInferenceDriver() must have been
    /// called on the same Runtime first; otherwise the "dsdriver" object is
    /// missing and this function returns InferenceNotInitialized.
    ///
    /// \param runtime         The Runtime whose "inference/dsdriver" object is reused.
    /// \param g2pPluginPaths  G2P plugin search directories (typically
    ///                         <pluginRoot>/srt-g2p/G2ps and <pluginRoot>/srt-g2p/dict).
    /// \return Expected<void> — InferenceNotInitialized if "dsdriver" missing,
    ///                         SessionError on plugin path / category failures.
    srt::core::Expected<void> SRT_G2P_EXPORT setupG2pOnnxDriver(
        srt::core::Runtime &runtime,
        const std::vector<std::filesystem::path> &g2pPluginPaths);

} // namespace srt::g2p
