#pragma once

#include <memory>

#include <synthrt/Audio/IAudioResampler.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// libswresample 重采样器实现（PIMPL）
    class SRT_AUDIO_EXPORT SwresampleResampler : public IAudioResampler {
    public:
        SwresampleResampler();
        ~SwresampleResampler() override;

        SwresampleResampler(SwresampleResampler &&) noexcept;
        SwresampleResampler &operator=(SwresampleResampler &&) noexcept;

        SwresampleResampler(const SwresampleResampler &) = delete;
        SwresampleResampler &operator=(const SwresampleResampler &) = delete;

        srt::core::Expected<AudioBuffer> convert(
            const AudioBuffer &input,
            int inputSampleRate,
            const ResampleConfig &config) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };

} // namespace srt::audio
