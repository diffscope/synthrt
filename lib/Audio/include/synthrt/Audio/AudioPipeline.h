#pragma once

#include <memory>
#include <string>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/AudioFormatInfo.h>
#include <synthrt/Audio/IAudioDecoder.h>
#include <synthrt/Audio/IAudioResampler.h>
#include <synthrt/Audio/ResampleConfig.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 解码 + 重采样便捷流水线
    ///
    /// 组合 IAudioDecoder 和 IAudioResampler，提供一站式 API。
    /// 内部流式处理（4096 帧/块），避免大文件一次性加载到内存。
    class SRT_AUDIO_EXPORT AudioPipeline {
    public:
        AudioPipeline(std::unique_ptr<IAudioDecoder> decoder,
                      std::unique_ptr<IAudioResampler> resampler);

        /// 使用默认 FFmpeg + swresample 实现
        static AudioPipeline create();

        srt::core::Expected<AudioFormatInfo> probe(const std::string &path);

        /// 解码并重采样整个文件
        srt::core::Expected<AudioBuffer> decodeAndResample(
            const std::string &path,
            const ResampleConfig &config);

        /// 解码指定时间段并重采样
        srt::core::Expected<AudioBuffer> decodeSegmentAndResample(
            const std::string &path,
            double startSec,
            double endSec,
            const ResampleConfig &config);

        /// 便捷方法：解码为单声道 float32
        srt::core::Expected<AudioBuffer> decodeToMonoFloat(
            const std::string &path,
            int targetSampleRate);

    private:
        std::unique_ptr<IAudioDecoder> m_decoder;
        std::unique_ptr<IAudioResampler> m_resampler;
    };

} // namespace srt::audio
