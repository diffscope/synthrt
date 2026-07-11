#include <synthrt/Audio/AudioPipeline.h>
#include <synthrt/Audio/FfmpegAudioDecoder.h>
#include <synthrt/Audio/SwresampleResampler.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace srt::audio {

namespace {
AudioBuffer mergeBuffers(const std::vector<AudioBuffer> &chunks) {
    if (chunks.empty()) {
        return AudioBuffer::create(0, 1, SampleFormat::Float32);
    }

    int channels = chunks[0].channelCount();
    SampleFormat fmt = chunks[0].format();

    int64_t totalFrames = 0;
    for (const auto &chunk : chunks) {
        totalFrames += chunk.frameCount();
    }

    // Allocate final buffer directly, avoiding intermediate vector + copy.
    auto result = AudioBuffer::create(totalFrames, channels, fmt);
    auto *dest = result.rawData();
    size_t offset = 0;
    for (const auto &chunk : chunks) {
        const auto bytes = chunk.byteSize();
        if (bytes > 0) {
            std::memcpy(dest + offset, chunk.rawData(), bytes);
            offset += bytes;
        }
    }

    return result;
}
} // anonymous namespace

AudioPipeline AudioPipeline::create() {
    return AudioPipeline(std::make_unique<FfmpegAudioDecoder>(), std::make_unique<SwresampleResampler>());
}

AudioPipeline::AudioPipeline(std::unique_ptr<IAudioDecoder> decoder, std::unique_ptr<IAudioResampler> resampler)
    : m_decoder(std::move(decoder)), m_resampler(std::move(resampler)) {
}

srt::core::Expected<AudioFormatInfo> AudioPipeline::probe(const std::string &path) {
    return m_decoder->probe(path);
}

srt::core::Expected<AudioBuffer> AudioPipeline::decodeAndResample(const std::string &path,
                                                                   const ResampleConfig &config) {
    // 1. Probe format
    auto infoExp = m_decoder->probe(path);
    if (!infoExp) {
        return infoExp.takeError();
    }
    auto info = infoExp.take();

    // 2. Open file
    auto openExp = m_decoder->open(path);
    if (!openExp) {
        return openExp.takeError();
    }

    // 3. Stream decode + resample in chunks
    constexpr int64_t kChunkFrames = 4096;
    std::vector<AudioBuffer> chunks;

    while (true) {
        auto chunkExp = m_decoder->read(kChunkFrames);
        if (!chunkExp) {
            return chunkExp.takeError();
        }
        auto chunk = chunkExp.take();
        if (chunk.empty())
            break; // EOF

        auto resampledExp = m_resampler->convert(chunk, info.sampleRate, config);
        if (!resampledExp) {
            return resampledExp.takeError();
        }
        chunks.push_back(resampledExp.take());
    }

    m_decoder->close();

    // 4. Merge all chunks
    if (chunks.empty()) {
        int outCh = config.targetChannelCount > 0 ? config.targetChannelCount : info.channelCount;
        auto outFmt = config.targetFormat != SampleFormat::Unknown ? config.targetFormat : info.sampleFormat;
        return AudioBuffer::create(0, outCh, outFmt);
    }
    return mergeBuffers(chunks);
}

srt::core::Expected<AudioBuffer> AudioPipeline::decodeSegmentAndResample(const std::string &path,
                                                                          double startSec,
                                                                          double endSec,
                                                                          const ResampleConfig &config) {
    // 1. Probe format
    auto infoExp = m_decoder->probe(path);
    if (!infoExp) {
        return infoExp.takeError();
    }
    auto info = infoExp.take();

    // 2. Open file
    auto openExp = m_decoder->open(path);
    if (!openExp) {
        return openExp.takeError();
    }

    // 3. Seek to start
    auto seekExp = m_decoder->seekToTime(startSec);
    if (!seekExp) {
        return seekExp.takeError();
    }

    // 4. Decode the segment
    double duration = endSec - startSec;
    int64_t targetFrames = static_cast<int64_t>(duration * info.sampleRate);

    constexpr int64_t kChunkFrames = 4096;
    int64_t remainingFrames = targetFrames;
    std::vector<AudioBuffer> chunks;

    while (remainingFrames > 0) {
        int64_t toRead = std::min(kChunkFrames, remainingFrames);
        auto chunkExp = m_decoder->read(toRead);
        if (!chunkExp) {
            return chunkExp.takeError();
        }
        auto chunk = chunkExp.take();
        if (chunk.empty())
            break;

        auto resampledExp = m_resampler->convert(chunk, info.sampleRate, config);
        if (!resampledExp) {
            return resampledExp.takeError();
        }
        chunks.push_back(resampledExp.take());
        remainingFrames -= chunk.frameCount();
    }

    m_decoder->close();

    if (chunks.empty()) {
        int outCh = config.targetChannelCount > 0 ? config.targetChannelCount : info.channelCount;
        auto outFmt = config.targetFormat != SampleFormat::Unknown ? config.targetFormat : info.sampleFormat;
        return AudioBuffer::create(0, outCh, outFmt);
    }
    return mergeBuffers(chunks);
}

srt::core::Expected<AudioBuffer> AudioPipeline::decodeToMonoFloat(const std::string &path, int targetSampleRate) {
    auto config = ResampleConfig::forMonoFloat(targetSampleRate);
    return decodeAndResample(path, config);
}

} // namespace srt::audio
