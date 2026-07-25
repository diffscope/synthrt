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

        // BUG-AUDIO-06: validate parameters to prevent division by zero
        // (bytesPerSample(Unknown) == 0) and invalid resampler state.
        if (srcCh <= 0 || dstCh <= 0 || srcRate <= 0 || dstRate <= 0 ||
            srcFmt == AV_SAMPLE_FMT_NONE || dstFmt == AV_SAMPLE_FMT_NONE) {
            return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                    "init: invalid resample parameters (srcCh=" + std::to_string(srcCh) +
                                        ", dstCh=" + std::to_string(dstCh) +
                                        ", srcRate=" + std::to_string(srcRate) +
                                        ", dstRate=" + std::to_string(dstRate) + ")");
        }

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
            cleanup(); // ROBUST-05 / CODING-04: 释放 swr_alloc_set_opts2 可能部分分配的 swrCtx
            return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                    "init: failed to set resampler options: " + ffmpegError(ret));
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
                                    "init: failed to initialize resampler: " + ffmpegError(ret));
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

    // BUG-AUDIO-06: defensive guard — init() already validates, but ensure
    // no division by zero at the outFrames computation and no overflow in
    // byte-size arithmetic (ROBUST-05).
    if (outCh <= 0 || dstBps <= 0 || srcBps <= 0) {
        return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                "convert: invalid sample format (outCh=" + std::to_string(outCh) +
                                    ", dstBps=" + std::to_string(dstBps) +
                                    ", srcBps=" + std::to_string(srcBps) + ")");
    }
    const size_t dstFrameBytes = static_cast<size_t>(outCh) * static_cast<size_t>(dstBps);
    const size_t srcFrameBytes = static_cast<size_t>(input.channelCount()) * static_cast<size_t>(srcBps);

    // Estimate output frames
    int64_t estimatedOutFrames = swr_get_out_samples(d->swrCtx, static_cast<int>(input.frameCount()));
    if (estimatedOutFrames <= 0) {
        estimatedOutFrames = input.frameCount();
    }

    std::vector<uint8_t> outData;
    outData.reserve(static_cast<size_t>(estimatedOutFrames) * dstFrameBytes);

    const auto *inData = reinterpret_cast<const uint8_t *>(input.rawData());
    // BUG-AUDIO-06: keep int64_t to avoid truncation on large inputs
    int64_t inFrames = input.frameCount();

    // Convert in chunks to avoid large intermediate buffers
    constexpr int64_t kChunkFrames = 4096;
    int64_t inOffset = 0;
    std::vector<uint8_t> chunkBuf; // Reused across iterations

    while (inOffset < inFrames) {
        int chunkFrames = static_cast<int>(std::min(kChunkFrames, inFrames - inOffset));
        const uint8_t *inPtr = inData + static_cast<size_t>(inOffset) * srcFrameBytes;

        int outSamples = swr_get_out_samples(d->swrCtx, chunkFrames);
        // BUG-AUDIO-07: swr_get_out_samples 可能返回负值，static_cast<size_t>(负数)
        // 会变成巨大无符号值，乘以 dstFrameBytes 后触发 std::bad_alloc / OOM。
        if (outSamples <= 0) {
            return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                    "convert: swr_get_out_samples returned " + std::to_string(outSamples));
        }
        chunkBuf.resize(static_cast<size_t>(outSamples) * dstFrameBytes);
        auto *outPtr = chunkBuf.data();

        int converted = swr_convert(d->swrCtx, &outPtr, outSamples, &inPtr, chunkFrames);
        if (converted < 0) {
            return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                    "convert: swr_convert failed: " + ffmpegError(converted));
        }
        if (converted > 0) {
            outData.insert(outData.end(), chunkBuf.begin(),
                           chunkBuf.begin() + static_cast<ptrdiff_t>(converted) * static_cast<ptrdiff_t>(dstFrameBytes));
        }

        inOffset += chunkFrames;
    }

    // Flush resampler
    {
        int outSamples = swr_get_out_samples(d->swrCtx, 0);
        if (outSamples > 0) {
            std::vector<uint8_t> flushBuf(static_cast<size_t>(outSamples) * dstFrameBytes);
            auto *outPtr = flushBuf.data();
            int converted = swr_convert(d->swrCtx, &outPtr, outSamples, nullptr, 0);
            if (converted < 0) {
                return srt::core::Error(srt::core::ErrorCode::AudioResampleFailed,
                                        "convert: swr_convert flush failed: " + ffmpegError(converted));
            }
            if (converted > 0) {
                outData.insert(outData.end(), flushBuf.begin(),
                               flushBuf.begin() + static_cast<ptrdiff_t>(converted) * static_cast<ptrdiff_t>(dstFrameBytes));
            }
        }
    }

    auto outFormat = fromAVSampleFormat(d->outFormat);
    // BUG-AUDIO-06: dstFrameBytes > 0 guaranteed by guard above (no div-by-zero)
    int64_t outFrames = static_cast<int64_t>(outData.size() / dstFrameBytes);
    return AudioBuffer::fromVector(std::move(outData), outFrames, outCh, outFormat);
}

} // namespace srt::audio
