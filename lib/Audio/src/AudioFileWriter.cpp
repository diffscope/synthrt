#include <synthrt/Audio/AudioFileWriter.h>
#include "FfmpegUtils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstring>

#include <synthrt/Core/Support/Error.h>

namespace srt::audio {
using internal::ffmpegError;
using internal::toAVSampleFormat;

struct AudioFileWriter::Impl {
    AVFormatContext *fmtCtx = nullptr;
    AVCodecContext *codecCtx = nullptr;
    AVStream *stream = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    SwrContext *swrCtx = nullptr;

    bool opened = false;
    int64_t framesWritten = 0;
    int sampleRate = 0;
    int channelCount = 0;
    SampleFormat format = SampleFormat::Unknown;

    // The encoder's required sample format (may differ from input)
    AVSampleFormat encoderFmt = AV_SAMPLE_FMT_NONE;
    int64_t pts = 0;

    void cleanup();
};

void AudioFileWriter::Impl::cleanup() {
    if (swrCtx) {
        swr_free(&swrCtx);
        swrCtx = nullptr;
    }
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
        if (opened && fmtCtx->pb) {
            av_write_trailer(fmtCtx);
        }
        if (!(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmtCtx->pb);
        }
        avformat_free_context(fmtCtx);
        fmtCtx = nullptr;
    }
    opened = false;
}

AudioFileWriter::AudioFileWriter() : d(std::make_unique<Impl>()) {
}
AudioFileWriter::~AudioFileWriter() {
    if (d && d->opened) {
        close();
    }
}

AudioFileWriter::AudioFileWriter(AudioFileWriter &&) noexcept = default;
AudioFileWriter &AudioFileWriter::operator=(AudioFileWriter &&) noexcept = default;

srt::core::Expected<void> AudioFileWriter::open(const std::string &path, int sampleRate, int channelCount,
                                                 SampleFormat format) {
    if (d->opened) {
        auto closeExp = close();
        if (!closeExp) {
            return closeExp.takeError();
        }
    }

    d->sampleRate = sampleRate;
    d->channelCount = channelCount;
    d->format = format;

    int ret = avformat_alloc_output_context2(&d->fmtCtx, nullptr, nullptr, path.c_str());
    if (ret < 0 || !d->fmtCtx) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "Failed to allocate output context: " + ffmpegError(ret));
    }

    // Find encoder
    const AVCodec *codec = avcodec_find_encoder(d->fmtCtx->oformat->audio_codec);
    if (!codec) {
        // Fallback to PCM s16le for WAV
        codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    }
    if (!codec) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed, "No suitable audio encoder found");
    }

    d->stream = avformat_new_stream(d->fmtCtx, nullptr);
    if (!d->stream) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed, "Failed to create audio stream");
    }

    d->codecCtx = avcodec_alloc_context3(codec);
    if (!d->codecCtx) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed, "Failed to allocate codec context");
    }

    d->codecCtx->sample_rate = sampleRate;
    av_channel_layout_default(&d->codecCtx->ch_layout, channelCount);

    // Determine encoder format
    d->encoderFmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLT;
    // Prefer float32 if encoder supports it
    if (codec->sample_fmts) {
        for (int i = 0; codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
            if (codec->sample_fmts[i] == AV_SAMPLE_FMT_FLT) {
                d->encoderFmt = AV_SAMPLE_FMT_FLT;
                break;
            }
        }
    }
    d->codecCtx->sample_fmt = d->encoderFmt;
    d->codecCtx->time_base = {1, sampleRate};

    ret = avcodec_open2(d->codecCtx, codec, nullptr);
    if (ret < 0) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "Failed to open encoder: " + ffmpegError(ret));
    }

    ret = avcodec_parameters_from_context(d->stream->codecpar, d->codecCtx);
    if (ret < 0) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "Failed to copy codec parameters: " + ffmpegError(ret));
    }
    d->stream->time_base = d->codecCtx->time_base;

    // Open output file
    if (!(d->fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&d->fmtCtx->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            d->cleanup();
            return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                    "Failed to open output file: " + ffmpegError(ret));
        }
    }

    ret = avformat_write_header(d->fmtCtx, nullptr);
    if (ret < 0) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                "Failed to write header: " + ffmpegError(ret));
    }

    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->packet || !d->frame) {
        d->cleanup();
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed, "Failed to allocate packet/frame");
    }

    // Setup resampler if input format differs from encoder format
    AVSampleFormat inputFmt = toAVSampleFormat(format);
    if (inputFmt != d->encoderFmt || inputFmt == AV_SAMPLE_FMT_NONE) {
        AVChannelLayout inChLayout{};
        av_channel_layout_default(&inChLayout, channelCount);
        AVChannelLayout outChLayout{};
        av_channel_layout_default(&outChLayout, channelCount);

        ret = swr_alloc_set_opts2(&d->swrCtx,
                                  &outChLayout,
                                  d->encoderFmt,
                                  sampleRate,
                                  &inChLayout,
                                  inputFmt == AV_SAMPLE_FMT_NONE ? AV_SAMPLE_FMT_FLT : inputFmt,
                                  sampleRate,
                                  0,
                                  nullptr);
        av_channel_layout_uninit(&inChLayout);
        av_channel_layout_uninit(&outChLayout);

        if (!d->swrCtx) {
            d->cleanup();
            return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed, "Failed to allocate resampler");
        }
        ret = swr_init(d->swrCtx);
        if (ret < 0) {
            d->cleanup();
            return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                    "Failed to init resampler: " + ffmpegError(ret));
        }
    }

    d->opened = true;
    d->framesWritten = 0;
    d->pts = 0;
    return {};
}

srt::core::Expected<void> AudioFileWriter::write(const AudioBuffer &buffer, int sampleRate) {
    (void)sampleRate; // Sample rate is set during open(); parameter kept for API consistency

    if (!d->opened) {
        return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed, "Writer not open");
    }

    if (buffer.empty()) {
        return {};
    }

    const int frameSize = d->codecCtx->frame_size > 0 ? d->codecCtx->frame_size : 1024;
    const auto *samples = buffer.rawData();
    int64_t frameCount = buffer.frameCount();
    int srcBps = bytesPerSample(buffer.format());

    int64_t offset = 0;
    while (offset < frameCount) {
        int64_t remaining = frameCount - offset;
        int nbSamples = static_cast<int>(std::min(static_cast<int64_t>(frameSize), remaining));

        d->frame->nb_samples = nbSamples;
        d->frame->format = d->encoderFmt;
        av_channel_layout_copy(&d->frame->ch_layout, &d->codecCtx->ch_layout);
        d->frame->pts = d->pts;

        int ret = av_frame_get_buffer(d->frame, 0);
        if (ret < 0) {
            return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                    "Failed to allocate frame buffer: " + ffmpegError(ret));
        }

        if (d->swrCtx) {
            const uint8_t *inData[1] = {samples + offset * d->channelCount * srcBps};
            ret = swr_convert(d->swrCtx, d->frame->data, nbSamples, inData, nbSamples);
            if (ret < 0) {
                av_frame_unref(d->frame);
                return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                        "Resample failed: " + ffmpegError(ret));
            }
        } else {
            int byteSize = nbSamples * d->channelCount * av_get_bytes_per_sample(d->encoderFmt);
            memcpy(d->frame->data[0], samples + offset * d->channelCount * srcBps, byteSize);
        }

        ret = avcodec_send_frame(d->codecCtx, d->frame);
        av_frame_unref(d->frame);
        if (ret < 0) {
            return srt::core::Error(srt::core::ErrorCode::AudioWriteFailed,
                                    "Failed to send frame: " + ffmpegError(ret));
        }

        while ((ret = avcodec_receive_packet(d->codecCtx, d->packet)) >= 0) {
            d->packet->stream_index = d->stream->index;
            av_packet_rescale_ts(d->packet, d->codecCtx->time_base, d->stream->time_base);
            av_interleaved_write_frame(d->fmtCtx, d->packet);
            av_packet_unref(d->packet);
        }

        d->pts += nbSamples;
        offset += nbSamples;
    }

    d->framesWritten += frameCount;
    return {};
}

srt::core::Expected<void> AudioFileWriter::close() {
    if (!d->opened)
        return {};

    // Flush encoder
    if (d->codecCtx) {
        avcodec_send_frame(d->codecCtx, nullptr);
        while (avcodec_receive_packet(d->codecCtx, d->packet) >= 0) {
            d->packet->stream_index = d->stream->index;
            av_packet_rescale_ts(d->packet, d->codecCtx->time_base, d->stream->time_base);
            av_interleaved_write_frame(d->fmtCtx, d->packet);
            av_packet_unref(d->packet);
        }
    }

    d->cleanup();
    return {};
}

} // namespace srt::audio
