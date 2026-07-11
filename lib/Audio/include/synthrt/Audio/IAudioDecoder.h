#pragma once

#include <string>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/AudioFormatInfo.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 音频解码器抽象接口（ARCH-02 依赖抽象）
    ///
    /// 实现类：FfmpegAudioDecoder（PIMPL，封装 FFmpeg C API）
    class SRT_AUDIO_EXPORT IAudioDecoder {
    public:
        virtual ~IAudioDecoder() = default;

        /// 探测文件格式（不打开文件）
        virtual srt::core::Expected<AudioFormatInfo> probe(const std::string &path) = 0;

        /// 打开文件
        virtual srt::core::Expected<void> open(const std::string &path) = 0;

        /// 关闭文件
        virtual void close() = 0;

        /// 读取一块音频（frameCount 帧的预算，返回实际帧数）
        /// 返回 empty buffer 表示 EOF
        virtual srt::core::Expected<AudioBuffer> read(int64_t frameCount) = 0;

        /// 跳转到指定时间（秒）
        virtual srt::core::Expected<void> seekToTime(double seconds) = 0;
    };

} // namespace srt::audio
