#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/Slicer.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/PitchExtractor.h>  // AudioRequirements
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::extract {

    /// 音频预处理编排
    ///
    /// 提取器内部使用，将任意格式的 AudioBuffer 重采样并切片为模型所需格式。
    class SRT_EXTRACT_EXPORT AudioPreprocessor {
    public:
        struct Slice {
            int64_t startFrame;          ///< 在原始（重采样后）音频中的起始帧
            int64_t endFrame;            ///< 结束帧
            std::vector<float> samples;  ///< 单声道 float PCM
        };

        /// 重采样 + 切片
        /// @param buffer 输入音频（任意格式）
        /// @param sampleRate 输入音频采样率
        /// @param requirements 模型所需格式
        /// @param slicer 切片器
        static srt::core::Expected<std::vector<Slice>> prepare(
            const srt::audio::AudioBuffer &buffer,
            int sampleRate,
            const AudioRequirements &requirements,
            const srt::audio::Slicer &slicer);

        /// 仅重采样到模型所需格式（返回单声道 float vector + 输出采样率）
        static srt::core::Expected<std::pair<std::vector<float>, int>> resampleToMono(
            const srt::audio::AudioBuffer &buffer,
            int sampleRate,
            const AudioRequirements &requirements);
    };

} // namespace srt::extract
