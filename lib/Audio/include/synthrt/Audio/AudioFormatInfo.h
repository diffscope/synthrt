#pragma once

#include <string>

#include <synthrt/Audio/SampleFormat.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 音频文件探测信息
    struct SRT_AUDIO_EXPORT AudioFormatInfo {
        int sampleRate = 0;            ///< 采样率（Hz）
        int channelCount = 0;          ///< 声道数
        SampleFormat sampleFormat = SampleFormat::Unknown;
        int64_t totalFrames = 0;       ///< 总帧数（0 表示未知）
        double durationSec = 0.0;      ///< 时长（秒）
        std::string codecName;         ///< 编码器名称
    };

} // namespace srt::audio
