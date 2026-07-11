#pragma once

#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// PCM 样本格式
    enum class SampleFormat {
        Unknown = 0,
        Float32, ///< 32-bit float
        Int16,   ///< 16-bit signed integer
        Int32,   ///< 32-bit signed integer
    };

    /// 每个样本的字节数
    inline int bytesPerSample(SampleFormat fmt) {
        switch (fmt) {
        case SampleFormat::Float32: return 4;
        case SampleFormat::Int16:   return 2;
        case SampleFormat::Int32:   return 4;
        default:                    return 0;
        }
    }

} // namespace srt::audio
