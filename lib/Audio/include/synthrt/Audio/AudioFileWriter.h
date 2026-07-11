#pragma once

#include <memory>
#include <string>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/SampleFormat.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 音频文件写入器（PIMPL，基于 FFmpeg muxer）
    class SRT_AUDIO_EXPORT AudioFileWriter {
    public:
        AudioFileWriter();
        ~AudioFileWriter();

        AudioFileWriter(AudioFileWriter &&) noexcept;
        AudioFileWriter &operator=(AudioFileWriter &&) noexcept;

        AudioFileWriter(const AudioFileWriter &) = delete;
        AudioFileWriter &operator=(const AudioFileWriter &) = delete;

        /// 打开输出文件
        /// @param path 输出文件路径（扩展名决定格式：.wav/.mp3/.flac 等）
        /// @param sampleRate 采样率
        /// @param channelCount 声道数
        /// @param format 样本格式
        srt::core::Expected<void> open(const std::string &path,
                                       int sampleRate,
                                       int channelCount,
                                       SampleFormat format);

        /// 写入音频数据
        srt::core::Expected<void> write(const AudioBuffer &buffer, int sampleRate);

        /// 关闭并 flush
        srt::core::Expected<void> close();

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };

} // namespace srt::audio
