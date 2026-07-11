# 04 - 接口定义（v2 修正）

日期: 2026-07-11（v2: 2026-07-11 修正）

定位: 汇总所有公开接口的完整定义，作为实施阶段的编码契约。本文档的代码片段可直接作为头文件模板。

**v1 → v2 主要修正：**
- AudioBuffer 改为 dataset-tools 的 view/owned 语义设计（不存储 sampleRate）
- 移除 SndfileVio（FFmpeg-based 不需要）
- AudioDecoder/Resampler 静态类 → IAudioDecoder/IAudioResampler 抽象接口 + FFmpeg 实现
- 新增 AudioPipeline 便捷流水线
- Extractor 基类取消，PitchExtractor/MidiExtractor 直接继承 NamedObject
- extract() 新增 sampleRate 参数（AudioBuffer 不存储采样率）
- MidiExtractor.extract() 改为接收 MidiExtractOptions
- 插件工厂用 IID 模式（staticIid()），移除 extractorType()
- 移除 ExtractSetup（不需要 ModuleCategory，用 PluginFactory.addPluginPath）
- Slicer 参数对照修正（rmvpe 第一参数实际为 hopSize）

---

## 1. Audio 模块接口

### 1.1 SampleFormat

```cpp
// include/synthrt/Audio/SampleFormat.h
#pragma once

#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// PCM 样本格式
    enum class SampleFormat {
        Unknown = 0,
        Float32,  ///< 32-bit float
        Int16,    ///< 16-bit signed integer
        Int32,    ///< 32-bit signed integer
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
```

### 1.2 AudioBuffer（view/owned 语义）

```cpp
// include/synthrt/Audio/AudioBuffer.h
#pragma once

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include <synthrt/Audio/SampleFormat.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// PCM 音频缓冲区，interleaved 格式
    ///
    /// 设计要点：
    /// - 支持 view（零拷贝，引用外部数据）和 owned（独立拥有数据）两种语义
    /// - 支持 Float32/Int16/Int32 多采样格式
    /// - **不存储 sampleRate**：采样率由调用方管理（AudioFormatInfo 或 extract 参数）
    /// - interleaved 布局：[L0, R0, L1, R1, ...]（stereo）
    class SRT_AUDIO_EXPORT AudioBuffer {
    public:
        AudioBuffer() = default;

        // -- 工厂方法 --

        /// 创建 owned 缓冲区（零初始化）
        static AudioBuffer create(int64_t frameCount, int channelCount, SampleFormat format);

        /// 从内存拷贝构造 owned 缓冲区
        static AudioBuffer fromCopy(const void *data, int64_t frameCount,
                                    int channelCount, SampleFormat format);

        /// 创建 view 缓冲区（零拷贝，引用外部数据）
        static AudioBuffer fromView(const void *data, int64_t frameCount,
                                    int channelCount, SampleFormat format);

        /// 从 vector 移动构造 owned 缓冲区
        static AudioBuffer fromVector(std::vector<uint8_t> &&data, int64_t frameCount,
                                      int channelCount, SampleFormat format);

        // -- 访问器 --

        int64_t frameCount() const noexcept { return m_frameCount; }
        int channelCount() const noexcept { return m_channelCount; }
        SampleFormat format() const noexcept { return m_format; }

        size_t byteSize() const;
        bool isView() const;
        bool empty() const noexcept { return m_frameCount == 0; }

        /// 时长（秒），需传入采样率（因为 AudioBuffer 不存储采样率）
        double durationSec(int sampleRate) const;

        // -- 数据访问 --

        const uint8_t *rawData() const;
        uint8_t *rawData();

        std::span<const float> floats() const;
        std::span<float> floats();

        std::span<const int16_t> int16s() const;
        std::span<int16_t> int16s();

        float sampleAt(int64_t frame, int channel) const;

        // -- 工具 --

        void zero();
        AudioBuffer clone() const;

        /// 切片（view 模式为零拷贝子 span，owned 模式为拷贝）
        AudioBuffer slice(int64_t startFrame, int64_t frameCount) const;

    private:
        std::variant<std::vector<uint8_t>, std::span<const uint8_t>> m_data;
        int64_t m_frameCount = 0;
        int m_channelCount = 0;
        SampleFormat m_format = SampleFormat::Unknown;
    };

} // namespace srt::audio
```

**设计要点：**
- `std::variant<vector, span>` 实现 view/owned 双语义（从 dataset-tools 迁移）
- view 模式：零拷贝，引用外部数据，不可修改
- owned 模式：独立拥有数据，可修改
- `slice()` 在 view 模式下返回子 span（零拷贝），owned 模式下拷贝
- **不存储 sampleRate**：调用方负责管理采样率信息

### 1.3 AudioFormatInfo

```cpp
// include/synthrt/Audio/AudioFormatInfo.h
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
```

### 1.4 ResampleConfig

```cpp
// include/synthrt/Audio/ResampleConfig.h
#pragma once

#include <synthrt/Audio/SampleFormat.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 重采样质量
    enum class ResampleQuality {
        Fast = 0,   ///< 快速（不启用 swresample 重采样，仅格式/声道转换）
        High = 1,   ///< 高质量（启用 SWR_FLAG_RESAMPLE）
    };

    /// 重采样配置
    struct SRT_AUDIO_EXPORT ResampleConfig {
        int targetSampleRate = 0;          ///< 目标采样率（0 = 保持原采样率）
        int targetChannelCount = 0;        ///< 目标声道数（0 = 保持原声道数）
        SampleFormat targetFormat = SampleFormat::Unknown;  ///< 目标格式（Unknown = 保持原格式）
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
```

### 1.5 IAudioDecoder 抽象接口

```cpp
// include/synthrt/Audio/IAudioDecoder.h
#pragma once

#include <string>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/AudioFormatInfo.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 音频解码器抽象接口（ARCH-02 依赖抽象）
    ///
    /// 实现类：FfmpegAudioDecoder（PIMPL，封装 FFmpeg C API）
    class SRT_AUDIO_EXPORT IAudioDecoder {
    public:
        virtual ~IAudioDecoder() = default;

        /// 探测文件格式（不打开文件）
        virtual srt::core::Expected<AudioFormatInfo> probe(const std::string &path) = 0;

        /// 打开文件
        virtual srt::core::Expected<void> open(const std::string &path) = 0;

        /// 关闭文件
        virtual void close() = 0;

        /// 读取一块音频（frameCount 帧的预算，返回实际帧数）
        /// 返回 empty buffer 表示 EOF
        virtual srt::core::Expected<AudioBuffer> read(int64_t frameCount) = 0;

        /// 跳转到指定时间（秒）
        virtual srt::core::Expected<void> seekToTime(double seconds) = 0;
    };

} // namespace srt::audio
```

### 1.6 IAudioResampler 抽象接口

```cpp
// include/synthrt/Audio/IAudioResampler.h
#pragma once

#include <utility>

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
```

### 1.7 FfmpegAudioDecoder（PIMPL 实现）

```cpp
// include/synthrt/Audio/FfmpegAudioDecoder.h
#pragma once

#include <memory>

#include <synthrt/Audio/IAudioDecoder.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// FFmpeg 解码器实现（PIMPL，FFmpeg 头文件不暴露到公开 API）
    class SRT_AUDIO_EXPORT FfmpegAudioDecoder : public IAudioDecoder {
    public:
        FfmpegAudioDecoder();
        ~FfmpegAudioDecoder() override;

        FfmpegAudioDecoder(FfmpegAudioDecoder &&) noexcept;
        FfmpegAudioDecoder &operator=(FfmpegAudioDecoder &&) noexcept;

        srt::core::Expected<AudioFormatInfo> probe(const std::string &path) override;
        srt::core::Expected<void> open(const std::string &path) override;
        void close() override;
        srt::core::Expected<AudioBuffer> read(int64_t frameCount) override;
        srt::core::Expected<void> seekToTime(double seconds) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };

} // namespace srt::audio
```

### 1.8 SwresampleResampler（PIMPL 实现）

```cpp
// include/synthrt/Audio/SwresampleResampler.h
#pragma once

#include <memory>

#include <synthrt/Audio/IAudioResampler.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// libswresample 重采样器实现（PIMPL）
    class SRT_AUDIO_EXPORT SwresampleResampler : public IAudioResampler {
    public:
        SwresampleResampler();
        ~SwresampleResampler() override;

        SwresampleResampler(SwresampleResampler &&) noexcept;
        SwresampleResampler &operator=(SwresampleResampler &&) noexcept;

        srt::core::Expected<AudioBuffer> convert(
            const AudioBuffer &input,
            int inputSampleRate,
            const ResampleConfig &config) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };

} // namespace srt::audio
```

### 1.9 AudioPipeline 便捷流水线

```cpp
// include/synthrt/Audio/AudioPipeline.h
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
```

### 1.10 AudioFileWriter

```cpp
// include/synthrt/Audio/AudioFileWriter.h
#pragma once

#include <memory>
#include <string>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Audio/srt_audio_global.h>

namespace srt::audio {

    /// 音频文件写入器（PIMPL，基于 FFmpeg muxer）
    class SRT_AUDIO_EXPORT AudioFileWriter {
    public:
        AudioFileWriter();
        ~AudioFileWriter();

        AudioFileWriter(AudioFileWriter &&) noexcept;
        AudioFileWriter &operator=(AudioFileWriter &&) noexcept;

        /// 打开输出文件
        /// @param path 输出文件路径（扩展名决定格式：.wav/.mp3/.flac 等）
        /// @param sampleRate 采样率
        /// @param channelCount 声道数
        /// @param format 样本格式
        srt::core::Expected<void> open(const std::string &path,
                                       int sampleRate,
                                       int channelCount,
                                       SampleFormat format);

        /// 写入音频数据
        srt::core::Expected<void> write(const AudioBuffer &buffer, int sampleRate);

        /// 关闭并 flush
        srt::core::Expected<void> close();

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };

} // namespace srt::audio
```

### 1.11 Slicer（从 audio-util 迁移）

```cpp
// include/synthrt/Audio/Slicer.h
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
```

**参数对照（从 lite 迁移，已修正）：**

| 用途 | sampleRate | threshold | hopSize | winSize | minLength | minInterval | maxSilKept | 说明 |
|---|---|---|---|---|---|---|---|---|
| rmvpe | 16000 | 0.02 | 160 | 640 | 500 | 30 | 50 | v1 原代码 `Slicer(160, ...)` 第一参数实为 hopSize 被误传为 sampleRate；修正为 16000 |
| game | tar_sr | 0.02 | 441 | 1764 | 200 | 30 | 50 | tar_sr 从 config.json 读取，默认 44100 |

---

## 2. Extract 模块接口

### 2.1 公共类型（定义在 PitchExtractor.h 中，供两个接口共享）

```cpp
// include/synthrt/Extract/PitchExtractor.h 顶部
namespace srt::extract {

    /// 模型所需音频格式
    struct AudioRequirements {
        int sampleRate = 0;   ///< 模型所需采样率（Hz）
        int channels = 0;     ///< 模型所需声道数（1=mono）
    };

    /// 进度回调（0-100）
    using ProgressCallback = std::function<void(int)>;

}
```

**设计说明：** 不设独立的 `Extractor` 基类。`PitchExtractor` 和 `MidiExtractor` 都直接继承 `srt::core::NamedObject`，接口完全独立。共享类型（`AudioRequirements`、`ProgressCallback`）定义在 `PitchExtractor.h` 中，`MidiExtractor.h` include 它。

### 2.2 PitchExtractor

```cpp
// include/synthrt/Extract/PitchExtractor.h
#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::extract {

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

    /// 模型所需音频格式
    struct AudioRequirements {
        int sampleRate = 0;
        int channels = 0;
    };

    /// 进度回调（0-100）
    using ProgressCallback = std::function<void(int)>;

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
```

**字段语义：**
- `offset`：该切片在原始音频中的时间偏移（毫秒），由 `slice.startFrame / (sampleRate / 1000)` 计算
- `f0`：基频序列（Hz），长度 = 切片帧数 / 帧移
- `uv`：清浊音标志，`true` = 浊音（voiced），`false` = 清音（unvoiced）

### 2.3 MidiExtractor

```cpp
// include/synthrt/Extract/MidiExtractor.h
#pragma once

#include <filesystem>
#include <functional>
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
```

**与 v1 方案的区别：**
- `extract()` 接收 `MidiExtractOptions` 而非单独的 `tempo` 参数
- 包含 Game 模型的所有可配置参数（segThreshold/estThreshold/language/d3pmTs）
- d3pmTs 为空时由插件内部自动生成（`generate_d3pm_ts()`）

### 2.4 插件工厂接口（IID 模式）

```cpp
// include/synthrt/Extract/PitchExtractorPlugin.h
#pragma once

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Extract/PitchExtractor.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract {

    /// IID 常量（类比 kInferenceDriverPluginIid = "srt.driver.InferenceDriver"）
    inline constexpr auto kPitchExtractorPluginIid = "srt.extract.PitchExtractor";

    /// 音高提取器插件工厂
    ///
    /// 由音高提取器插件 DLL 实现。PluginFactory 按 IID 加载，
    /// lite 通过此工厂创建 PitchExtractor 实例。
    class SRT_EXTRACT_EXPORT PitchExtractorPlugin : public srt::core::Plugin {
    public:
        PitchExtractorPlugin();
        ~PitchExtractorPlugin() override;

        const char *iid() const override { return staticIid(); }

        /// 静态 IID 访问器 — 允许 PluginFactory::plugin<T>(key) 无需实例即可获取 IID
        static const char *staticIid() { return kPitchExtractorPluginIid; }

        /// 创建音高提取器实例
        /// @param runtime Runtime 实例（提取器通过它获取 ONNX 驱动）
        /// @return 新的 PitchExtractor 实例（未 open）
        virtual srt::core::Expected<srt::core::NO<PitchExtractor>>
        createExtractor(srt::core::Runtime *runtime) = 0;

        STDCORELIB_DISABLE_COPY(PitchExtractorPlugin)
    };

} // namespace srt::extract
```

```cpp
// include/synthrt/Extract/MidiExtractorPlugin.h
#pragma once

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Extract/MidiExtractor.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract {

    inline constexpr auto kMidiExtractorPluginIid = "srt.extract.MidiExtractor";

    class SRT_EXTRACT_EXPORT MidiExtractorPlugin : public srt::core::Plugin {
    public:
        MidiExtractorPlugin();
        ~MidiExtractorPlugin() override;

        const char *iid() const override { return staticIid(); }
        static const char *staticIid() { return kMidiExtractorPluginIid; }

        virtual srt::core::Expected<srt::core::NO<MidiExtractor>>
        createExtractor(srt::core::Runtime *runtime) = 0;

        STDCORELIB_DISABLE_COPY(MidiExtractorPlugin)
    };

} // namespace srt::extract
```

**关键设计点（v2 修正）：**
- `staticIid()` 返回常量 IID，允许 `PluginFactory::plugin<T>(key)` 在不实例化的情况下获取 IID
- `createExtractor(Runtime*)` 传入 Runtime：提取器需要 Runtime 来获取 ONNX 驱动
- **没有 `extractorType()` 方法**：IID 本身区分类型
- 继承 `Plugin`：与 `InferenceDriverPlugin` 完全一致的模式

### 2.5 ExtractorDriver 辅助函数

```cpp
// include/synthrt/Extract/ExtractorDriver.h
#pragma once

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract {

    /// 从 Runtime 获取 ONNX 推理驱动
    ///
    /// 提取器插件用此函数获取已注册的 dsdriver。
    /// 只检查 backend == "onnx"，不检查 arch（提取器不是 DiffSinger 模型）。
    SRT_EXTRACT_EXPORT
    srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>>
    getInferenceDriver(const srt::core::Runtime *runtime);

} // namespace srt::extract
```

**实现逻辑（从 RmvpeModel.cpp:13-44 迁移，去掉 arch 检查）：**
```cpp
srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>>
getInferenceDriver(const srt::core::Runtime *runtime) {
    if (!runtime) {
        return Error(ErrorCode::SessionError, "Runtime is nullptr");
    }
    auto cate = runtime->moduleCategory("inference");
    if (!cate) {
        return Error(ErrorCode::DriverNotFound, "inference category not found");
    }
    auto obj = cate->getFirstObject("dsdriver");
    if (!obj) {
        return Error(ErrorCode::DriverNotFound, "dsdriver not found");
    }
    auto driver = obj.as<srt::driver::InferenceDriver>();
    // 只检查 backend，不检查 arch（提取器不是 diffsinger）
    if (driver->backend() != srt::driver::onnx::API_NAME) {
        return Error(ErrorCode::DriverNotFound, "backend is not onnx");
    }
    return driver;
}
```

### 2.6 AudioPreprocessor

```cpp
// include/synthrt/Extract/AudioPreprocessor.h
#pragma once

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
            int64_t startFrame;       ///< 在原始（重采样后）音频中的起始帧
            int64_t endFrame;         ///< 结束帧
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
```

**实现逻辑：**
1. `resampleToMono`：用 `SwresampleResampler` 重采样到 requirements 指定的采样率/单声道，提取 float 数据
2. `prepare`：先 `resampleToMono`，再用 `Slicer.slice()` 切片，构造 Slice 列表

---

## 3. 接口设计原则

### 3.1 接口分离

- `PitchExtractor` 和 `MidiExtractor` 完全独立，无继承关系（都继承 NamedObject）
- f0 提取器插件只实现 `PitchExtractor`，MIDI 提取器插件只实现 `MidiExtractor`
- 未来新增提取器类型（如 onset 检测）定义新的 `OnsetExtractor` 接口和对应 Plugin IID

### 3.2 错误处理

所有 API 返回 `srt::core::Expected<T>`，错误使用 `srt::core::Error` 构造，自动捕获 `std::source_location::current()`。

| 场景 | 返回类型 | ErrorCode |
|---|---|---|
| 文件解码失败 | `Expected<AudioBuffer>` | `AudioDecodeFailed` (700) |
| 重采样失败 | `Expected<AudioBuffer>` | `AudioResampleFailed` (701) |
| 不支持的音频格式 | `Expected<AudioFormatInfo>` | `AudioUnsupportedFormat` (702) |
| 无效的 AudioBuffer | `Expected<T>` | `AudioInvalidBuffer` (703) |
| 音频文件写入失败 | `Expected<void>` | `AudioWriteFailed` (704) |
| 提取器未初始化 | `Expected<T>` | `ExtractNotInitialized` (800) |
| 模型打开失败 | `Expected<void>` | `ExtractModelOpenFailed` (801) |
| 推理失败 | `Expected<T>` | `ExtractInferenceFailed` (802) |
| 输出无效 | `Expected<T>` | `ExtractOutputInvalid` (803) |
| 提取器插件未找到 | 由 PluginFactory 返回 | `ExtractPluginNotFound` (804) |
| 不支持的模型版本 | `Expected<T>` | `ExtractUnsupportedVersion` (805) |
| Runtime 为空 | `Expected<T>` | `SessionError` |
| ONNX 驱动未找到 | `Expected<T>` | `DriverNotFound` |

### 3.3 内存管理

- 提取器实例通过 `srt::core::NO<T>` 引用计数管理（继承 `std::shared_ptr<T>`）
- lite 持有 `NO<PitchExtractor>` 延长生命周期
- 提取器内部 ONNX session 由提取器自己管理（open 创建，close 释放）
- `AudioBuffer` 支持 view 语义，避免不必要拷贝

### 3.4 线程安全

- 单个提取器实例**不可并发调用** `extract()`
- 多个提取器实例**可并行**（各自持有独立 ONNX session）
- lite 通过 Qt 任务队列保证单实例串行调用

### 3.5 PIMPL 隔离（INFRA-13）

- `FfmpegAudioDecoder`、`SwresampleResampler`、`AudioFileWriter` 使用 PIMPL
- FFmpeg/swresample 的 C 头文件不暴露到公开 API
- 公开头文件只包含 `<memory>`（用于 `std::unique_ptr<Impl>`）

---

## 4. lite 侧适配接口

lite 不直接使用上述 C++ 接口，而是通过 Qt Task 封装。以下为 lite 适配层的设计（不在 synthrt 中实现）：

### 4.1 ExtractPitchTask 适配

```cpp
// lite 侧：ExtractPitchTask.cpp（修改后）
void ExtractPitchTask::runTask() {
    // 1. 获取 PluginFactory
    auto *plugins = m_inferEngine->runtime().services().get<srt::core::PluginFactory>();

    // 2. 获取 rmvpe 插件（按 IID + key 查找）
    auto *rmvpePlugin = plugins->plugin<srt::extract::PitchExtractorPlugin>("rmvpe");
    if (!rmvpePlugin) { /* 错误处理 */ }

    // 3. 创建提取器实例（传入 Runtime）
    auto extractorExp = rmvpePlugin->createExtractor(&m_inferEngine->runtime());
    if (!extractorExp) { /* 错误处理 */ }
    auto extractor = extractorExp.take();

    // 4. 打开模型
    if (auto exp = extractor->open(m_modelPath); !exp) { /* 错误处理 */ }

    // 5. 解码音频文件为 AudioBuffer（FFmpeg-based）
    srt::audio::AudioPipeline pipeline = srt::audio::AudioPipeline::create();
    auto infoExp = pipeline.probe(m_audioPath.toStdString());
    if (!infoExp) { /* 错误处理 */ }
    auto info = infoExp.take();

    // 解码到原始格式（提取器内部自动重采样）
    auto bufferExp = pipeline.decodeAndResample(
        m_audioPath.toStdString(),
        srt::audio::ResampleConfig::forMonoFloat(info.sampleRate));
    if (!bufferExp) { /* 错误处理 */ }
    auto buffer = bufferExp.take();

    // 6. 提取（传入采样率，提取器内部自动重采样到模型所需格式）
    auto resultExp = extractor->extract(buffer, info.sampleRate, [this](int p) {
        updateProgress(p);
    });
    if (!resultExp) { /* 错误处理 */ }

    // 7. 转换为 lite 内部格式
    auto result = resultExp.take();
    for (const auto &frame : result.frames) {
        // frame.offset（毫秒）, frame.f0（Hz vector）, frame.uv（bool vector）
        // 转换为 lite 的 QList<QPair<double, QList<double>>>
    }

    // 8. 关闭
    extractor->close();
}
```

### 4.2 ExtractMidiTask 适配

```cpp
// lite 侧：ExtractMidiTask.cpp（修改后）
void ExtractMidiTask::runTask() {
    auto *plugins = m_inferEngine->runtime().services().get<srt::core::PluginFactory>();
    auto *gamePlugin = plugins->plugin<srt::extract::MidiExtractorPlugin>("game");
    auto extractorExp = gamePlugin->createExtractor(&m_inferEngine->runtime());
    auto extractor = extractorExp.take();

    extractor->open(m_modelPath);

    // 解码音频
    srt::audio::AudioPipeline pipeline = srt::audio::AudioPipeline::create();
    auto infoExp = pipeline.probe(m_audioPath.toStdString());
    auto info = infoExp.take();
    auto bufferExp = pipeline.decodeAndResample(
        m_audioPath.toStdString(),
        srt::audio::ResampleConfig::forMonoFloat(info.sampleRate));
    auto buffer = bufferExp.take();

    // 配置提取参数
    srt::extract::MidiExtractOptions options;
    options.tempo = m_tempo;
    options.segThreshold = m_segThreshold;
    options.estThreshold = m_estThreshold;
    options.language = m_language;
    // d3pmTs 为空，由插件内部自动生成

    // 提取
    auto resultExp = extractor->extract(buffer, info.sampleRate, options, [this](int p) {
        updateProgress(p);
    });
    auto result = resultExp.take();

    // 转换 MidiNote 列表为 lite 内部格式
    for (const auto &note : result.notes) {
        // note.note, note.start, note.duration
    }

    extractor->close();
}
```

### 4.3 lite 初始化注册插件路径

```cpp
// lite 侧：SynthrtEngine.cpp（修改后）
void SynthrtEngine::initializeExtractPlugins() {
    auto *plugins = m_runtime.services().get<srt::core::PluginFactory>();

    // 注册提取器插件搜索路径（与 InferenceDriver 插件路径模式一致）
    const auto extractPluginDir = (m_pluginRoot / "srt-extract").string();
    plugins->addPluginPath(srt::extract::kPitchExtractorPluginIid, extractPluginDir);
    plugins->addPluginPath(srt::extract::kMidiExtractorPluginIid, extractPluginDir);
}
```

### 4.4 lite 不再需要的代码

迁移后 lite 可删除：
- `src/libs/rmvpe-infer/` 整个目录
- `src/libs/game-infer/` 整个目录
- `src/libs/audio-util/` 整个目录（Slicer 已迁移到 synthrt）
- `ExtractPitchTask` 和 `ExtractMidiTask` 中的 rmvpe/game 直接引用

### 4.5 lite vcpkg 依赖变更

lite 的 vcpkg.json 移除：
- `sndfile` / `SndFile`
- `soxr`
- `mpg123`
- `FLAC`
- `xsimd`（如无其他用途）

这些依赖转移到 synthrt 的 vcpkg.json（仅 `ffmpeg`，替代上述全部）。
