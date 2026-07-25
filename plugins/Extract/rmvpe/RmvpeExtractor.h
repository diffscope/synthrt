#pragma once

#include <filesystem>
#include <vector>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Extract/PitchExtractor.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract::plugins::Rmvpe {

/// RmvpeExtractor - 基于 RMVPE ONNX 模型的 PitchExtractor 实现。
///
/// 从 ds-editor-lite rmvpe-infer 迁移。使用单个 ONNX session，
/// 输入 (waveform, threshold)，输出 (f0, uv)。音频重采样到
/// 16000 Hz 单声道，并由 RMS 切片器切片后推理。
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
    /// ONNX 前向推理（从 RmvpeModel.cpp:132-177 迁移）。
    srt::core::Expected<void> forward(
        const std::vector<float> &waveform, float threshold,
        std::vector<float> &f0, std::vector<bool> &uv);

    /// 清音段的 f0 插值（从 Rmvpe.cpp:35-99 迁移）。
    static void interpF0(std::vector<float> &f0, std::vector<bool> &uv);

    srt::core::Runtime *m_runtime = nullptr;
    srt::core::NO<srt::driver::InferenceDriver> m_driver;
    srt::core::NO<srt::driver::InferenceSession> m_session;
};

}
