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
        using DataStorage = std::variant<std::vector<uint8_t>, std::span<const uint8_t>>;

        const uint8_t *dataPtr() const;
        uint8_t *dataPtrMutable();

        DataStorage m_data;
        int64_t m_frameCount = 0;
        int m_channelCount = 0;
        SampleFormat m_format = SampleFormat::Unknown;
    };

} // namespace srt::audio
