#pragma once

#include <memory>

#include <synthrt/Audio/IAudioDecoder.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// FFmpeg 解码器实现（PIMPL，FFmpeg 头文件不暴露到公开 API）
    class SRT_AUDIO_EXPORT FfmpegAudioDecoder : public IAudioDecoder {
    public:
        FfmpegAudioDecoder();
        ~FfmpegAudioDecoder() override;

        FfmpegAudioDecoder(FfmpegAudioDecoder &&) noexcept;
        FfmpegAudioDecoder &operator=(FfmpegAudioDecoder &&) noexcept;

        FfmpegAudioDecoder(const FfmpegAudioDecoder &) = delete;
        FfmpegAudioDecoder &operator=(const FfmpegAudioDecoder &) = delete;

        srt::core::Expected<AudioFormatInfo> probe(const std::string &path) override;
        srt::core::Expected<void> open(const std::string &path) override;
        void close() override;
        srt::core::Expected<AudioBuffer> read(int64_t frameCount) override;
        srt::core::Expected<void> seekToTime(double seconds) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };

} // namespace srt::audio
