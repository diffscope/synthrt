#include <synthrt/Audio/FfmpegAudioDecoder.h>

#include "FfmpegUtils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>

#include <libswresample/swresample.h>
}

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Error.h>

namespace srt::audio {
using internal::ffmpegError;

struct FfmpegAudioDecoder::Impl {
    AVFormatContext *fmtCtx = nullptr;
    AVCodecContext *codecCtx = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;

    int audioStreamIdx = -1;
    bool opened = false;
    bool eof = false;

    // BUG-AUDIO-02: accumulated decoder error so decodeNextFrame (whose
    // signature cannot change per D-11) can surface failures via read()
    // instead of silently `continue`-ing. Reset at the start of each read().
    srt::core::Error m_lastError;

    AudioFormatInfo info;
    double currentPosSec = 0.0;
    double totalDurationSec = 0.0;

    void cleanup();
    double calcDuration() const;
    bool decodeNextFrame(std::vector<uint8_t> &outData, int64_t targetFrames);
};

void FfmpegAudioDecoder::Impl::cleanup() {
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
    if (codecCtx) {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }
    if (fmtCtx) {
        avformat_close_input(&fmtCtx);
        fmtCtx = nullptr;
    }
    audioStreamIdx = -1;
    opened = false;
    eof = false;
    currentPosSec = 0.0;
    totalDurationSec = 0.0;
    m_lastError = srt::core::Error();
}

double FfmpegAudioDecoder::Impl::calcDuration() const {
    if (!fmtCtx || audioStreamIdx < 0)
        return 0.0;
    AVStream *stream = fmtCtx->streams[audioStreamIdx];
    if (stream->duration > 0) {
        return static_cast<double>(stream->duration) * av_q2d(stream->time_base);
    }
    if (fmtCtx->duration > 0) {
        return static_cast<double>(fmtCtx->duration) / AV_TIME_BASE;
    }
    return 0.0;
}

bool FfmpegAudioDecoder::Impl::decodeNextFrame(std::vector<uint8_t> &outData, int64_t targetFrames) {
    if (!fmtCtx || !codecCtx || eof)
        return false;

    int bps = bytesPerSample(info.sampleFormat);
    int channels = info.channelCount;
    std::vector<uint8_t> planarBuf; // Reused across frames to avoid per-frame allocation

    while (static_cast<int64_t>(outData.size() / (bps * channels)) < targetFrames) {
        int readRet = av_read_frame(fmtCtx, packet);
        if (readRet < 0) {
            if (readRet == AVERROR_EOF) {
                eof = true;
                break;
            }
            // BUG-AUDIO-02: real read error — record and stop (ROBUST-05)
            av_packet_unref(packet);
            m_lastError = srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                           "decodeNextFrame: av_read_frame failed: " + ffmpegError(readRet));
            return false;
        }
        if (packet->stream_index != audioStreamIdx) {
            av_packet_unref(packet);
            continue;
        }
        int ret = avcodec_send_packet(codecCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            // BUG-AUDIO-02: real send_packet error — record and stop (ROBUST-05)
            m_lastError = srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                           "decodeNextFrame: avcodec_send_packet failed: " + ffmpegError(ret));
            return false;
        }

        // BUG-AUDIO-02: distinguish EAGAIN/EOF (normal) from real decode
        // errors (previously any non-zero return was silently ignored).
        while (true) {
            int recvRet = avcodec_receive_frame(codecCtx, frame);
            if (recvRet != 0) {
                if (recvRet != AVERROR(EAGAIN) && recvRet != AVERROR_EOF) {
                    av_frame_unref(frame);
                    m_lastError = srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                                   "decodeNextFrame: avcodec_receive_frame failed: " + ffmpegError(recvRet));
                    return false;
                }
                break; // EAGAIN (need more input) or EOF (decoder flushed)
            }

            // Copy raw frame data in source format (no resampling).
            // Use frame->format (actual decoded format) instead of codecCtx->sample_fmt
            // (configured format) — they may differ for some decoders.
            const auto frameFmt = static_cast<AVSampleFormat>(frame->format);

            // BUG-01: Validate decoded frame format against decoder context.
            // Some decoders (e.g. certain AAC/MP3 decoders) ignore codecPar->format
            // and output a different sample format (e.g. FLTP instead of declared S16).
            // The bps used below comes from info.sampleFormat (mapped from codecPar->format
            // at open() time); if the actual frame format's bps differs, frameBytes will
            // be wrong, causing OOB reads (declared bps > actual bps) or silent data
            // corruption (declared bps < actual bps) in both planar and interleaved
            // branches below. Detect the mismatch and fail fast instead of reading the
            // buffer with the wrong byte count. (ROBUST-05, ROBUST-02)
            const int actualBps = av_get_bytes_per_sample(frameFmt);
            if (actualBps <= 0) {
                av_frame_unref(frame);
                m_lastError = srt::core::Error(
                    srt::core::ErrorCode::AudioDecodeFailed,
                    "decodeNextFrame: invalid decoded frame sample format: " +
                        std::to_string(static_cast<int>(frameFmt)));
                return false;
            }
            if (actualBps != bps) {
                av_frame_unref(frame);
                m_lastError = srt::core::Error(
                    srt::core::ErrorCode::AudioDecodeFailed,
                    "decodeNextFrame: decoded frame sample format " +
                        std::to_string(static_cast<int>(frameFmt)) +
                        " (bps=" + std::to_string(actualBps) +
                        ") does not match decoder context format " +
                        std::to_string(static_cast<int>(
                            internal::toAVSampleFormat(info.sampleFormat))) +
                        " (bps=" + std::to_string(bps) + ")");
                return false;
            }

            const int frameChannels = frame->ch_layout.nb_channels;
            // BUG-AUDIO-03: int64_t to avoid overflow on large frames
            const int64_t frameBytes = static_cast<int64_t>(frame->nb_samples) * frameChannels * bps;
            if (av_sample_fmt_is_planar(frameFmt)) {
                // Planar: deinterleave to interleaved
                planarBuf.resize(static_cast<size_t>(frameBytes));
                for (int ch = 0; ch < frameChannels; ++ch) {
                    const uint8_t *src = frame->extended_data[ch];
                    auto *dst = planarBuf.data() + static_cast<ptrdiff_t>(ch) * bps;
                    for (int s = 0; s < frame->nb_samples; ++s) {
                        std::memcpy(dst + static_cast<ptrdiff_t>(s) * frameChannels * bps,
                                    src + static_cast<ptrdiff_t>(s) * bps, bps);
                    }
                }
                outData.insert(outData.end(), planarBuf.begin(), planarBuf.end());
            } else {
                // Interleaved: direct copy
                outData.insert(outData.end(), frame->data[0], frame->data[0] + frameBytes);
            }
            av_frame_unref(frame);
        }
    }

    return !outData.empty();
}

// ===== Public API =====

FfmpegAudioDecoder::FfmpegAudioDecoder() : d(std::make_unique<Impl>()) {
}

FfmpegAudioDecoder::~FfmpegAudioDecoder() {
    close();
}

FfmpegAudioDecoder::FfmpegAudioDecoder(FfmpegAudioDecoder &&) noexcept = default;
FfmpegAudioDecoder &FfmpegAudioDecoder::operator=(FfmpegAudioDecoder &&) noexcept = default;

srt::core::Expected<AudioFormatInfo> FfmpegAudioDecoder::probe(const std::string &path) {
    AVFormatContext *probeCtx = nullptr;
    int ret = avformat_open_input(&probeCtx, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "Failed to open file for probing: " + path + " (" + ffmpegError(ret) + ")");
    }

    ret = avformat_find_stream_info(probeCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&probeCtx);
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "probe: failed to find stream info for '" + path +
                                    "': " + ffmpegError(ret));
    }

    int streamIdx = av_find_best_stream(probeCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIdx < 0) {
        avformat_close_input(&probeCtx);
        return srt::core::Error(srt::core::ErrorCode::AudioUnsupportedFormat,
                                "probe: no audio stream found in '" + path + "'");
    }

    auto *codecPar = probeCtx->streams[streamIdx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);

    AudioFormatInfo info;
    info.sampleRate = codecPar->sample_rate;
    info.channelCount = codecPar->ch_layout.nb_channels;

    auto fmt = internal::fromAVSampleFormat(static_cast<AVSampleFormat>(codecPar->format));
    info.sampleFormat = (fmt != SampleFormat::Unknown) ? fmt : SampleFormat::Float32;

    AVStream *stream = probeCtx->streams[streamIdx];
    if (stream->duration > 0) {
        info.durationSec = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
        info.totalFrames = static_cast<int64_t>(info.durationSec * info.sampleRate);
    } else if (probeCtx->duration > 0) {
        info.durationSec = static_cast<double>(probeCtx->duration) / AV_TIME_BASE;
        info.totalFrames = static_cast<int64_t>(info.durationSec * info.sampleRate);
    } else {
        info.durationSec = 0.0;
        info.totalFrames = 0;
    }
    if (codec) {
        info.codecName = codec->name ? codec->name : "unknown";
    }

    avformat_close_input(&probeCtx);
    return info;
}

srt::core::Expected<void> FfmpegAudioDecoder::open(const std::string &path) {
    close();

    int ret = avformat_open_input(&d->fmtCtx, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "Failed to open file: " + path + " (" + ffmpegError(ret) + ")");
    }

    ret = avformat_find_stream_info(d->fmtCtx, nullptr);
    if (ret < 0) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "open: failed to find stream info for '" + path +
                                    "': " + ffmpegError(ret));
    }

    d->audioStreamIdx = av_find_best_stream(d->fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (d->audioStreamIdx < 0) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioUnsupportedFormat,
                                "open: no audio stream found in '" + path + "'");
    }

    auto *codecPar = d->fmtCtx->streams[d->audioStreamIdx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioUnsupportedFormat,
                                "open: no decoder found for codec id " +
                                    std::to_string(codecPar->codec_id) + " in '" + path + "'");
    }

    d->codecCtx = avcodec_alloc_context3(codec);
    if (!d->codecCtx) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "open: failed to allocate codec context for '" + path + "'");
    }
    avcodec_parameters_to_context(d->codecCtx, codecPar);
    ret = avcodec_open2(d->codecCtx, codec, nullptr);
    if (ret < 0) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "open: failed to open codec '" +
                                    std::string(codec->name ? codec->name : "unknown") +
                                    "' for '" + path + "': " + ffmpegError(ret));
    }

    // Fill format info
    d->info.sampleRate = codecPar->sample_rate;
    d->info.channelCount = codecPar->ch_layout.nb_channels;
    auto fmt2 = internal::fromAVSampleFormat(static_cast<AVSampleFormat>(codecPar->format));
    d->info.sampleFormat = (fmt2 != SampleFormat::Unknown) ? fmt2 : SampleFormat::Float32;

    d->totalDurationSec = d->calcDuration();
    d->info.durationSec = d->totalDurationSec;
    if (d->totalDurationSec > 0.0) {
        d->info.totalFrames = static_cast<int64_t>(d->totalDurationSec * d->info.sampleRate);
    } else {
        d->info.totalFrames = 0;
    }

    if (codec && codec->name) {
        d->info.codecName = codec->name;
    }

    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    d->opened = true;
    d->eof = false;
    d->currentPosSec = 0.0;

    return {};
}

void FfmpegAudioDecoder::close() {
    if (d)
        d->cleanup();
}

srt::core::Expected<AudioBuffer> FfmpegAudioDecoder::read(int64_t frameCount) {
    if (frameCount <= 0)
        return AudioBuffer::create(0, d->info.channelCount, d->info.sampleFormat);
    if (!d->opened) {
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "read: no file is open");
    }
    if (d->eof) {
        return AudioBuffer::create(0, d->info.channelCount, d->info.sampleFormat);
    }

    int bps = bytesPerSample(d->info.sampleFormat);
    int channels = d->info.channelCount;

    std::vector<uint8_t> data;
    data.reserve(static_cast<size_t>(frameCount * channels * bps));

    // BUG-AUDIO-02: reset accumulated error before each decode pass; if
    // decodeNextFrame records a real error, propagate it via Expected
    // (ROBUST-01 / ROBUST-05) instead of returning an empty buffer.
    d->m_lastError = srt::core::Error();
    bool gotData = d->decodeNextFrame(data, frameCount);

    if (!d->m_lastError.ok()) {
        return d->m_lastError;
    }

    if (data.empty() && !gotData) {
        return AudioBuffer::create(0, channels, d->info.sampleFormat);
    }

    int64_t framesRead = static_cast<int64_t>(data.size() / (bps * channels));
    d->currentPosSec += static_cast<double>(framesRead) / d->info.sampleRate;

    return AudioBuffer::fromVector(std::move(data), framesRead, channels, d->info.sampleFormat);
}

srt::core::Expected<void> FfmpegAudioDecoder::seekToTime(double seconds) {
    if (!d->opened) {
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "seekToTime: no file is open");
    }

    AVStream *stream = d->fmtCtx->streams[d->audioStreamIdx];
    int64_t timestamp =
        av_rescale_q(static_cast<int64_t>(seconds * AV_TIME_BASE), av_make_q(1, AV_TIME_BASE), stream->time_base);

    int ret = av_seek_frame(d->fmtCtx, d->audioStreamIdx, timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return srt::core::Error(srt::core::ErrorCode::AudioDecodeFailed,
                                "Failed to seek to time: " + std::to_string(seconds) + " (" + ffmpegError(ret) + ")");
    }

    avcodec_flush_buffers(d->codecCtx);
    d->eof = false;
    d->currentPosSec = seconds;

    return {};
}

} // namespace srt::audio
