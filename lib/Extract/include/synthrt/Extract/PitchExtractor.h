#ifndef SRT_EXTRACT_PITCHEXTRACTOR_H
#define SRT_EXTRACT_PITCHEXTRACTOR_H

#include <filesystem>
#include <functional>
#include <vector>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::extract {

    /// 模型所需音频格式
    struct AudioRequirements {
        int sampleRate = 0;   ///< 模型所需采样率（Hz）
        int channels = 0;     ///< 模型所需声道数（1=mono）
    };

    /// 进度回调（0-100）
    using ProgressCallback = std::function<void(int)>;

    /// 单个切片的音高提取结果
    struct PitchFrame {
        float offset = 0.0f;        ///< 时间偏移（毫秒）
        std::vector<float> f0;      ///< 基频序列（Hz）
        std::vector<bool> uv;       ///< 清浊音标志（true=浊音 voiced）
    };

    /// 音高提取结果
    struct PitchResult {
        std::vector<PitchFrame> frames;  ///< 按切片组织的音高帧
    };

    /// 音高提取器接口
    ///
    /// 所有音高提取模型（rmvpe 及未来其他算法）实现此接口。
    /// 继承 NamedObject 以支持 NO<PitchExtractor> 引用计数。
    class SRT_EXTRACT_EXPORT PitchExtractor : public srt::core::NamedObject {
    public:
        virtual ~PitchExtractor() = default;

        /// 打开模型
        virtual srt::core::Expected<void> open(const std::filesystem::path &modelPath) = 0;

        /// 是否已打开
        virtual bool isOpen() const = 0;

        /// 关闭模型，释放资源
        virtual void close() = 0;

        /// 终止当前推理
        virtual void terminate() = 0;

        /// 获取模型所需的音频格式要求
        /// 在 open() 成功后调用
        virtual AudioRequirements audioRequirements() const = 0;

        /// 提取音高
        /// @param buffer 输入音频（任意采样率/声道数，内部自动重采样）
        /// @param sampleRate 输入音频的采样率（AudioBuffer 不存储采样率）
        /// @param progress 进度回调
        virtual srt::core::Expected<PitchResult> extract(
            const srt::audio::AudioBuffer &buffer,
            int sampleRate,
            const ProgressCallback &progress = {}) = 0;
    };

} // namespace srt::extract

#endif // SRT_EXTRACT_PITCHEXTRACTOR_H
