#pragma once

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/ResampleConfig.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 音频重采样器抽象接口
    ///
    /// 实现类：SwresampleResampler（PIMPL，封装 libswresample C API）
    class SRT_AUDIO_EXPORT IAudioResampler {
    public:
        virtual ~IAudioResampler() = default;

        /// 转换音频缓冲区
        /// @param input 输入缓冲区
        /// @param inputSampleRate 输入采样率（AudioBuffer 不存储采样率）
        /// @param config 目标配置
        virtual srt::core::Expected<AudioBuffer> convert(
            const AudioBuffer &input,
            int inputSampleRate,
            const ResampleConfig &config) = 0;
    };

} // namespace srt::audio
