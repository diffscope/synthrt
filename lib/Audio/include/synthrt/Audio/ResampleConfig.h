#pragma once

#include <synthrt/Audio/SampleFormat.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 重采样质量
    enum class ResampleQuality {
        Fast = 0, ///< 快速（不启用 swresample 重采样，仅格式/声道转换）
        High = 1, ///< 高质量（启用 SWR_FLAG_RESAMPLE）
    };

    /// 重采样配置
    struct SRT_AUDIO_EXPORT ResampleConfig {
        int targetSampleRate = 0;          ///< 目标采样率（0 = 保持原采样率）
        int targetChannelCount = 0;        ///< 目标声道数（0 = 保持原声道数）
        SampleFormat targetFormat = SampleFormat::Unknown; ///< 目标格式（Unknown = 保持原格式）
        ResampleQuality quality = ResampleQuality::High;

        /// 便捷配置：单声道 float32
        static ResampleConfig forMonoFloat(int sampleRate) {
            return {sampleRate, 1, SampleFormat::Float32, ResampleQuality::High};
        }

        /// 便捷配置：立体声 float32
        static ResampleConfig forStereoFloat(int sampleRate) {
            return {sampleRate, 2, SampleFormat::Float32, ResampleQuality::High};
        }
    };

} // namespace srt::audio
