#ifndef RMVPE_EXTRACTOR_H
#define RMVPE_EXTRACTOR_H

#include <filesystem>
#include <vector>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Extract/PitchExtractor.h>

namespace srt::core {
    class Runtime;
}

/// RmvpeExtractor - PitchExtractor implementation backed by the RMVPE ONNX model.
///
/// Migrated from ds-editor-lite rmvpe-infer. Uses a single ONNX session that
/// takes (waveform, threshold) and returns (f0, uv). Audio is resampled to
/// 16000 Hz mono and sliced by an RMS slicer before inference.
class RmvpeExtractor : public srt::extract::PitchExtractor {
public:
    explicit RmvpeExtractor(srt::core::Runtime *runtime);

    srt::core::Expected<void> open(const std::filesystem::path &modelPath) override;
    bool isOpen() const override;
    void close() override;
    void terminate() override;
    srt::extract::AudioRequirements audioRequirements() const override;

    srt::core::Expected<srt::extract::PitchResult> extract(
        const srt::audio::AudioBuffer &buffer,
        int sampleRate,
        const srt::extract::ProgressCallback &progress) override;

private:
    /// ONNX forward pass (migrated from RmvpeModel.cpp:132-177).
    srt::core::Expected<void> forward(
        const std::vector<float> &waveform, float threshold,
        std::vector<float> &f0, std::vector<bool> &uv);

    /// f0 interpolation for unvoiced gaps (migrated from Rmvpe.cpp:35-99).
    static void interpF0(std::vector<float> &f0, std::vector<bool> &uv);

    srt::core::Runtime *m_runtime = nullptr;
    srt::core::NO<srt::driver::InferenceDriver> m_driver;
    srt::core::NO<srt::driver::InferenceSession> m_session;
};

#endif // RMVPE_EXTRACTOR_H
