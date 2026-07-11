#ifndef SYNTHRT_PLUGIN_EXTRACT_GAME_GAMEEXTRACTOR_H
#define SYNTHRT_PLUGIN_EXTRACT_GAME_GAMEEXTRACTOR_H

#include <filesystem>
#include <vector>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Extract/MidiExtractor.h>

namespace srt::core {
    class Runtime;
}

/// GameExtractor - MIDI 提取器实现（从 ds-editor-lite game-infer 迁移）。
///
/// 管理 4 个 ONNX session（encoder/segmenter/estimator/bd2dur），
/// 逐切片执行 4 阶段推理并构建 MIDI 音符序列。
class GameExtractor : public srt::extract::MidiExtractor {
public:
    explicit GameExtractor(srt::core::Runtime *runtime);

    ~GameExtractor() override;

    srt::core::Expected<void> open(const std::filesystem::path &modelPath) override;
    bool isOpen() const override;
    void close() override;
    void terminate() override;
    srt::extract::AudioRequirements audioRequirements() const override;

    srt::core::Expected<srt::extract::MidiResult> extract(
        const srt::audio::AudioBuffer &buffer,
        int sampleRate,
        const srt::extract::MidiExtractOptions &options,
        const srt::extract::ProgressCallback &progress) override;

private:
    // -- 推理辅助结构 --
    struct InferenceOutput {
        std::vector<uint8_t> boundaries;  ///< [1, T] 边界标志
        std::vector<float> durations;     ///< [1, N] 持续时间（秒）
        std::vector<float> presence;      ///< [1, N] 置信度
        std::vector<float> scores;        ///< [1, N] MIDI 音高
        std::vector<uint8_t> maskN;       ///< [1, N] 有效音符标志
    };

    // -- 4 阶段推理（从 GameModel.cpp 迁移） --

    /// encoder: 音频编码器，输出 x_seg / x_est / maskT
    std::tuple<srt::core::NO<srt::core::ITensor>,
               srt::core::NO<srt::core::ITensor>,
               srt::core::NO<srt::core::ITensor>>
    runEncoder(const std::vector<float> &waveform, float duration) const;

    /// segmenter: 分割器，输出 boundaries
    std::vector<uint8_t> runSegmenter(
        const srt::core::NO<srt::core::ITensor> &xSeg,
        const std::vector<uint8_t> &knownBoundaries,
        const std::vector<uint8_t> &prevBoundaries,
        int language,
        const srt::core::NO<srt::core::ITensor> &maskT,
        float threshold, int radius,
        const std::vector<float> &d3pmTs) const;

    /// bd2dur: 边界到持续时间，输出 (durations, maskN)
    std::tuple<std::vector<float>, std::vector<uint8_t>>
    runBd2dur(const std::vector<uint8_t> &boundaries,
              const std::vector<uint8_t> &maskT) const;

    /// estimator: 估计器，输出 (presence, scores)
    std::tuple<std::vector<float>, std::vector<float>>
    runEstimator(const srt::core::NO<srt::core::ITensor> &xEst,
                 const std::vector<uint8_t> &boundaries,
                 const srt::core::NO<srt::core::ITensor> &maskT,
                 const std::vector<uint8_t> &maskN,
                 float threshold) const;

    /// 编排 4 阶段推理流程（从 GameModel::inferSlice 迁移）
    InferenceOutput inferSlice(const std::vector<float> &waveform,
                               float duration, int language,
                               float segThreshold, int segRadiusFrames,
                               float estThreshold,
                               const std::vector<float> &d3pmTs) const;

    // -- 静态辅助（从 Game.cpp 迁移） --

    /// D3PM 时间步生成（原 generate_d3pm_ts(0.0f, 8)）
    static std::vector<float> generateD3pmTs();

    /// MIDI 音符构建（原 build_midi_note）
    static std::vector<srt::extract::MidiNote> buildMidiNotes(
        int startTick, const std::vector<float> &durations,
        const std::vector<float> &presence, const std::vector<float> &scores,
        float tempo);

    /// tick 计算（原 calculateNoteTicks）
    static std::vector<int> calculateNoteTicks(
        const std::vector<float> &noteDurations, float tempo);

    /// 累加和（原 cumulativeSum）
    static std::vector<double> cumulativeSum(const std::vector<float> &durations);

private:
    srt::core::Runtime *m_runtime = nullptr;
    srt::core::NO<srt::driver::InferenceDriver> m_driver;

    // 4 个 ONNX session
    srt::core::NO<srt::driver::InferenceSession> m_encoder;
    srt::core::NO<srt::driver::InferenceSession> m_segmenter;
    srt::core::NO<srt::driver::InferenceSession> m_estimator;
    srt::core::NO<srt::driver::InferenceSession> m_bd2dur;

    // 从 config.json 读取的参数
    int m_targetSampleRate = 44100;  ///< 目标采样率（config.json "samplerate"）
    float m_timestep = 0.01f;        ///< 时间步（config.json "timestep"）
    float m_segThreshold = 0.2f;     ///< 分割阈值（config.json "seg_threshold"）
    float m_segRadiusSeconds = 0.02f;///< 分割半径（config.json "seg_radius_seconds"）
    float m_estThreshold = 0.2f;     ///< 估计阈值（config.json "est_threshold"）
    int m_language = 0;              ///< 语言（config.json "languages"）
};

#endif // SYNTHRT_PLUGIN_EXTRACT_GAME_GAMEEXTRACTOR_H
