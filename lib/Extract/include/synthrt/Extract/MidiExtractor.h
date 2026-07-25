#pragma once

#include <filesystem>
#include <vector>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/PitchExtractor.h>  // AudioRequirements, ProgressCallback
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::extract {

    /// 单个 MIDI 音符
    struct MidiNote {
        int note = 0;          ///< MIDI 音高编号 (0-127)
        int start = 0;         ///< 起始 tick（基于 480 PPQ）
        int duration = 0;      ///< 持续 tick
    };

    /// MIDI 提取结果
    struct MidiResult {
        std::vector<MidiNote> notes;  ///< 提取的音符序列
    };

    /// MIDI 提取器可配置参数（对应 Game 模型的所有参数）
    struct MidiExtractOptions {
        float tempo = 120.0f;               ///< 速度（BPM），tick 计算用
        float segThreshold = 0.2f;          ///< 分割阈值
        float segRadiusSeconds = 0.02f;     ///< 分割半径（秒）
        float estThreshold = 0.2f;          ///< 估计阈值
        int language = 0;                   ///< 语言（0=默认）
        std::vector<float> d3pmTs;          ///< D3PM 时间步（空则自动生成）
    };

    /// MIDI 提取器接口
    ///
    /// 所有 MIDI 提取模型（game 及未来其他算法）实现此接口。
    class SRT_EXTRACT_EXPORT MidiExtractor : public srt::core::NamedObject {
    public:
        virtual ~MidiExtractor() = default;

        virtual srt::core::Expected<void> open(const std::filesystem::path &modelPath) = 0;
        virtual bool isOpen() const = 0;
        virtual void close() = 0;
        virtual void terminate() = 0;
        virtual AudioRequirements audioRequirements() const = 0;

        /// 提取 MIDI 音符
        /// @param buffer 输入音频
        /// @param sampleRate 输入音频的采样率
        /// @param options 提取参数（tempo、阈值等）
        /// @param progress 进度回调
        virtual srt::core::Expected<MidiResult> extract(
            const srt::audio::AudioBuffer &buffer,
            int sampleRate,
            const MidiExtractOptions &options,
            const ProgressCallback &progress = {}) = 0;
    };

} // namespace srt::extract
