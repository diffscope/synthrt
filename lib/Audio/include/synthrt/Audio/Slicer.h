#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    using MarkerList = std::vector<std::pair<int64_t, int64_t>>;

    /// RMS 静音切片器
    ///
    /// 从 ds-editor-lite audio-util 迁移，用于音频分段（去除静音）。
    /// namespace: AudioUtil → srt::audio
    /// 导出宏: AUDIO_UTIL_EXPORT → SRT_AUDIO_EXPORT
    class SRT_AUDIO_EXPORT Slicer {
    public:
        Slicer(int sampleRate, float threshold, int hopSize, int winSize,
               int minLength, int minInterval, int maxSilKept);

        /// 对单声道 float PCM 执行切片
        /// @return 切片标记列表 [(startFrame, endFrame), ...]
        MarkerList slice(const std::vector<float> &samples) const;

    private:
        int m_sampleRate;
        float m_threshold;
        int m_hopSize;
        int m_winSize;
        int m_minLength;
        int m_minInterval;
        int m_maxSilKept;

        static std::vector<double> getRms(
            const std::vector<float> &samples,
            int frameLength, int hopLength);
    };

} // namespace srt::audio
