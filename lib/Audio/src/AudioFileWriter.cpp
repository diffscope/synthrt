#include <synthrt/Audio/AudioFileWriter.h>

#include <sndfile.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include <synthrt/Core/Support/Error.h>

namespace srt::audio {

// ----------------------------------------------------------------------------
// libsndfile 后端实现（P4-2）。
//
// 设计要点：
// - 公共 API（open/write/close）保持不变，PIMPL 隔离 libsndfile 头文件
// - SF_FORMAT 根据 path 扩展名 + SampleFormat 决定（.wav/.flac/.ogg/.aiff）
// - write() 根据 buffer.format() 调用 sf_writef_float / sf_writef_short /
//   sf_writef_int，无需调用方手动转换
// - 保留 P2-11 修复的 AFW-2/AFW-3/AFW-4 输入校验
// - FFmpeg 依赖仅保留在 FfmpegAudioDecoder（解码侧），写入侧不再使用 FFmpeg
// ----------------------------------------------------------------------------

namespace {

// 提取文件扩展名（小写），返回包含点的 view（如 ".wav"），无扩展名返回空 view。
std::string_view getExtension(std::string_view path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string_view::npos) {
        return {};
    }
    auto ext = path.substr(pos);
    // 转小写（不修改原 path）
    static thread_local std::string lower;
    lower.clear();
    lower.reserve(ext.size());
    for (char c : ext) {
        lower.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    }
    return lower;
}

} // namespace

struct AudioFileWriter::Impl {
    SNDFILE *sndfile = nullptr;
    SF_INFO info = {};
    int sampleRate = 0;
    int channelCount = 0;
    SampleFormat format = SampleFormat::Unknown;
    bool opened = false;

    ~Impl() {
        // 防御性：析构时若仍打开则强制 close（不传播错误）
        if (sndfile) {
            sf_close(sndfile);
            sndfile = nullptr;
        }
    }
};

AudioFileWriter::AudioFileWriter() : d(std::make_unique<Impl>()) {
}

AudioFileWriter::~AudioFileWriter() {
    if (d && d->opened) {
        close();
    }
}

AudioFileWriter::AudioFileWriter(AudioFileWriter &&) noexcept = default;
AudioFileWriter &AudioFileWriter::operator=(AudioFileWriter &&) noexcept = default;

srt::core::Expected<void> AudioFileWriter::open(const std::string &path, int sampleRate,
                                                 int channelCount, SampleFormat format) {
    // AFW-2: 重复 open 时先 close 旧实例
    if (d->opened) {
        auto closeExp = close();
        if (!closeExp) {
            return closeExp.takeError();
        }
    }

    // 校验参数
    if (sampleRate <= 0) {
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "open: invalid sampleRate (" + std::to_string(sampleRate) +
                                    ") for '" + path + "'");
    }
    if (channelCount <= 0) {
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "open: invalid channelCount (" + std::to_string(channelCount) +
                                    ") for '" + path + "'");
    }
    // AFW-3: format 必须非 Unknown
    if (format == SampleFormat::Unknown) {
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "open: sample format is Unknown for '" + path + "'");
    }

    // 根据扩展名 + format 决定 SF_FORMAT
    const auto ext = getExtension(path);
    int majorFormat = 0;
    int subFormat = 0;

    if (ext == ".wav") {
        majorFormat = SF_FORMAT_WAV;
        switch (format) {
        case SampleFormat::Float32: subFormat = SF_FORMAT_FLOAT; break;
        case SampleFormat::Int16:   subFormat = SF_FORMAT_PCM_16; break;
        case SampleFormat::Int32:   subFormat = SF_FORMAT_PCM_32; break;
        default:                    subFormat = SF_FORMAT_FLOAT; break;
        }
    } else if (ext == ".flac") {
        majorFormat = SF_FORMAT_FLAC;
        switch (format) {
        case SampleFormat::Int16:   subFormat = SF_FORMAT_PCM_16; break;
        case SampleFormat::Int32:   subFormat = SF_FORMAT_PCM_24; break;
        case SampleFormat::Float32: subFormat = SF_FORMAT_FLOAT; break;
        default:                    subFormat = SF_FORMAT_PCM_24; break;
        }
    } else if (ext == ".ogg") {
        majorFormat = SF_FORMAT_OGG;
        subFormat = SF_FORMAT_VORBIS;
    } else if (ext == ".aiff" || ext == ".aif") {
        majorFormat = SF_FORMAT_AIFF;
        switch (format) {
        case SampleFormat::Float32: subFormat = SF_FORMAT_FLOAT; break;
        case SampleFormat::Int16:   subFormat = SF_FORMAT_PCM_16; break;
        case SampleFormat::Int32:   subFormat = SF_FORMAT_PCM_32; break;
        default:                    subFormat = SF_FORMAT_FLOAT; break;
        }
    } else {
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "open: unsupported file extension '" + std::string(ext) +
                                    "' for '" + path + "' (supported: .wav/.flac/.ogg/.aiff)");
    }

    d->info = {};
    d->info.samplerate = sampleRate;
    d->info.channels = channelCount;
    d->info.format = majorFormat | subFormat;

    d->sndfile = sf_open(path.c_str(), SFM_WRITE, &d->info);
    if (!d->sndfile) {
        return srt::core::Error(
            srt::core::ErrorCode::AudioWriteFailed,
            "open: sf_open failed for '" + path + "': " + sf_strerror(nullptr));
    }

    d->sampleRate = sampleRate;
    d->channelCount = channelCount;
    d->format = format;
    d->opened = true;
    return {};
}

srt::core::Expected<void> AudioFileWriter::write(const AudioBuffer &buffer, int sampleRate) {
    // AFW-1: sampleRate 在 open() 时固定，此处忽略。调用方应确保 buffer 的实际采样率
    // 与 open() 一致，否则写出的文件采样率错误。
    (void)sampleRate;

    if (!d->opened) {
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "write: writer is not open");
    }

    if (buffer.empty()) {
        return {};
    }

    // AFW-2: 校验 buffer 通道数与 writer 一致
    if (buffer.channelCount() != d->channelCount) {
        return srt::core::Error(
            srt::core::ErrorCode::AudioWriteFailed,
            "write: channel count mismatch (buffer=" + std::to_string(buffer.channelCount()) +
                ", writer=" + std::to_string(d->channelCount) + ")");
    }

    // AFW-3: 校验 buffer format 非 Unknown
    if (buffer.format() == SampleFormat::Unknown) {
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "write: buffer sample format is Unknown");
    }

    // libsndfile 接受 interleaved 数据，与 AudioBuffer 布局一致。
    // 根据 buffer.format() 调用对应 API。注意：open() 时确定的 subFormat 必须与
    // buffer.format() 兼容（libsndfile 会自动转换 int16/int32/float，但调用方传
    // 入的 SampleFormat 应与 open() 时一致以保证写出的 PCM 类型正确）。
    const auto frameCount = static_cast<sf_count_t>(buffer.frameCount());
    sf_count_t written = 0;

    switch (buffer.format()) {
    case SampleFormat::Float32: {
        const auto *data = buffer.floats().data();
        written = sf_writef_float(d->sndfile, data, frameCount);
        break;
    }
    case SampleFormat::Int16: {
        const auto *data = buffer.int16s().data();
        written = sf_writef_short(d->sndfile, data, frameCount);
        break;
    }
    case SampleFormat::Int32: {
        // AudioBuffer 暂未提供 int32s() 访问器，用 rawData 按 int32_t 解释
        const auto *data = reinterpret_cast<const int32_t *>(buffer.rawData());
        written = sf_writef_int(d->sndfile, data, frameCount);
        break;
    }
    default:
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "write: unsupported sample format in buffer");
    }

    if (written != frameCount) {
        return srt::core::Error(
            srt::core::ErrorCode::AudioWriteFailed,
            "write: sf_writef_* wrote " + std::to_string(written) + "/" +
                std::to_string(frameCount) + " frames: " + sf_strerror(d->sndfile));
    }
    return {};
}

srt::core::Expected<void> AudioFileWriter::close() {
    if (!d->opened) {
        return {};
    }

    // sf_write_sync 刷新内部缓冲（libsndfile 默认会缓冲部分数据）
    sf_write_sync(d->sndfile);

    int err = sf_close(d->sndfile);
    d->sndfile = nullptr;
    d->opened = false;
    if (err != 0) {
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "close: sf_close failed: " + std::string(sf_strerror(nullptr)));
    }
    return {};
}

} // namespace srt::audio
