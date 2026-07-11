#include <synthrt/Audio/SwresampleResampler.h>
#include "FfmpegUtils.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstring>
#include <vector>

#include <synthrt/Core/Support/Error.h>

namespace srt::audio {
using internal::ffmpegError;
using internal::fromAVSampleFormat;
using internal::toAVSampleFormat;

namespace {
int qualityToAvFlags(ResampleQuality quality) {
    switch (quality) {
    case ResampleQuality::Fast:
        return 0;
    case ResampleQuality::High:
        return SWR_FLAG_RESAMPLE;
    default:
        return 0;
    }
}
} // anonymous namespace

struct SwresampleResampler::Impl {
    SwrContext *swrCtx = nullptr;
    int64_t inSampleRate = 0;
    int inChannels = 0;
    AVSampleFormat inFormat = AV_SAMPLE_FMT_FLT;
    int64_t outSampleRate = 0;
    int outChannels = 0;
    AVSampleFormat outFormat = AV_SAMPLE_FMT_FLT;

    void cleanup() {
        if (swrCtx) {
            swr_free(&swrCtx);
            swrCtx = nullptr;
        }
    }

    srt::core::Expected<void> init(const AudioBuffer &input, int inputSampleRate, const ResampleConfig &config) {
        auto srcFmt = toAVSampleFormat(input.format());
        auto dstFmt = (config.targetFormat != SampleFormat::Unknown) ? toAVSampleFormat(config.targetFormat) : srcFmt;
        auto srcRate = inputSampleRate;
        auto dstRate = (config.targetSampleRate > 0) ? config.targetSampleRate : srcRate;
        auto srcCh = input.channelCount();
        auto dstCh = (config.targetChannelCount > 0) ? config.targetChannelCount : srcCh;

        // If no change needed, skip swresample
        bool same = (srcFmt == dstFmt) && (srcRate == dstRate) && (srcCh == dstCh);
        if (same) {
            inSampleRate = srcRate;
            inChannels = srcCh;
            inFormat = srcFmt;
            outSampleRate = dstRate;
            outChannels = dstCh;
            outFormat = dstFmt;
            return {};
        }

        // Reuse existing swrCtx if parameters haven't changed.
        // This preserves the resampler's internal delay state across chunked
        // convert() calls (e.g. AudioPipeline's 4096-frame streaming decode),
        // preventing audio artifacts at chunk boundaries.
        if (swrCtx &&
            inSampleRate == srcRate && inChannels == srcCh && inFormat == srcFmt &&
            outSampleRate == dstRate && outChannels == dstCh && outFormat == dstFmt) {
            return {};
        }

        cleanup();

        AVChannelLayout inChLayout{};
        av_channel_layout_default(&inChLayout, srcCh);
        AVChannelLayout outChLayout{};
        av_channel_layout_default(&outChLayout, dstCh);

        int ret = swr_alloc_set_opts2(&swrCtx, &outChLayout, dstFmt, dstRate, &inChLayout, srcFmt, srcRate, 0, nullptr);
        av_channel_layout_uninit(&inChLayout);
        av_channel_layout_uninit(&outChLayout);

        if (ret < 0) {
            return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                    "Failed to set resampler options: " + ffmpegError(ret));
        }

        // Apply quality flags
        int flags = qualityToAvFlags(config.quality);
        if (flags) {
            av_opt_set_int(swrCtx, "flags", flags, 0);
        }

        ret = swr_init(swrCtx);
        if (ret < 0) {
            cleanup();
            return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                    "Failed to initialize resampler: " + ffmpegError(ret));
        }

        inSampleRate = srcRate;
        inChannels = srcCh;
        inFormat = srcFmt;
        outSampleRate = dstRate;
        outChannels = dstCh;
        outFormat = dstFmt;

        return {};
    }
};

SwresampleResampler::SwresampleResampler() : d(std::make_unique<Impl>()) {
}

SwresampleResampler::~SwresampleResampler() {
    if (d)
        d->cleanup();
}

SwresampleResampler::SwresampleResampler(SwresampleResampler &&) noexcept = default;
SwresampleResampler &SwresampleResampler::operator=(SwresampleResampler &&) noexcept = default;

srt::core::Expected<AudioBuffer>
SwresampleResampler::convert(const AudioBuffer &input, int inputSampleRate, const ResampleConfig &config) {
    if (input.empty()) {
        return AudioBuffer::create(0,
                                   config.targetChannelCount > 0 ? config.targetChannelCount : input.channelCount(),
                                   config.targetFormat != SampleFormat::Unknown ? config.targetFormat : input.format());
    }

    auto initExp = d->init(input, inputSampleRate, config);
    if (!initExp) {
        return initExp.takeError();
    }

    // If no resampling needed (same format), return copy
    if (!d->swrCtx) {
        return input.clone();
    }

    // Perform resampling
    int srcBps = bytesPerSample(input.format());
    int dstBps = bytesPerSample(fromAVSampleFormat(d->outFormat));
    int outCh = d->outChannels;

    // Estimate output frames
    int64_t estimatedOutFrames = swr_get_out_samples(d->swrCtx, static_cast<int>(input.frameCount()));
    if (estimatedOutFrames <= 0) {
        estimatedOutFrames = input.frameCount();
    }

    std::vector<uint8_t> outData;
    outData.reserve(static_cast<size_t>(estimatedOutFrames * outCh * dstBps));

    const auto *inData = reinterpret_cast<const uint8_t *>(input.rawData());
    int inFrames = static_cast<int>(input.frameCount());

    // Convert in chunks to avoid large intermediate buffers
    constexpr int kChunkFrames = 4096;
    int inOffset = 0;
    std::vector<uint8_t> chunkBuf; // Reused across iterations

    while (inOffset < inFrames) {
        int chunkFrames = std::min(kChunkFrames, inFrames - inOffset);
        const uint8_t *inPtr = inData + inOffset * input.channelCount() * srcBps;

        int outSamples = swr_get_out_samples(d->swrCtx, chunkFrames);
        chunkBuf.resize(outSamples * outCh * dstBps);
        auto *outPtr = chunkBuf.data();

        int converted = swr_convert(d->swrCtx, &outPtr, outSamples, &inPtr, chunkFrames);
        if (converted < 0) {
            return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                    "swr_convert failed: " + ffmpegError(converted));
        }
        if (converted > 0) {
            outData.insert(outData.end(), chunkBuf.begin(), chunkBuf.begin() + converted * outCh * dstBps);
        }

        inOffset += chunkFrames;
    }

    // Flush resampler
    {
        int outSamples = swr_get_out_samples(d->swrCtx, 0);
        if (outSamples > 0) {
            std::vector<uint8_t> flushBuf(outSamples * outCh * dstBps);
            auto *outPtr = flushBuf.data();
            int converted = swr_convert(d->swrCtx, &outPtr, outSamples, nullptr, 0);
            if (converted < 0) {
                return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                        "swr_convert flush failed: " + ffmpegError(converted));
            }
            if (converted > 0) {
                outData.insert(outData.end(), flushBuf.begin(), flushBuf.begin() + converted * outCh * dstBps);
            }
        }
    }

    auto outFormat = fromAVSampleFormat(d->outFormat);
    int64_t outFrames = static_cast<int64_t>(outData.size() / (outCh * dstBps));
    return AudioBuffer::fromVector(std::move(outData), outFrames, outCh, outFormat);
}

} // namespace srt::audio
