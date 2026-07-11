// AudioPreprocessor.cpp - Audio preprocessing orchestration for extractors.
//
// Provides resampling (via SwresampleResampler) and slicing (via Slicer) so
// that extractor implementations can feed model-ready mono float PCM to their
// ONNX sessions. Depends on srt::audio types (Phase 1, lib/Audio).

#include <synthrt/Extract/AudioPreprocessor.h>

#include <utility>
#include <vector>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/ResampleConfig.h>
#include <synthrt/Audio/Slicer.h>
#include <synthrt/Audio/SwresampleResampler.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/PitchExtractor.h>

namespace srt::extract {

    srt::core::Expected<std::pair<std::vector<float>, int>>
    AudioPreprocessor::resampleToMono(
        const srt::audio::AudioBuffer &buffer,
        int sampleRate,
        const AudioRequirements &requirements) {

        if (requirements.sampleRate <= 0) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                "AudioPreprocessor::resampleToMono: invalid target sample rate");
        }

        // Resample to mono float32 at the model-required sample rate.
        srt::audio::SwresampleResampler resampler;
        auto config = srt::audio::ResampleConfig::forMonoFloat(requirements.sampleRate);

        auto resampledExp = resampler.convert(buffer, sampleRate, config);
        if (!resampledExp) {
            return resampledExp.takeError();
        }

        auto resampled = resampledExp.take();
        auto spans = resampled.floats();

        std::vector<float> samples(spans.begin(), spans.end());
        return std::make_pair(std::move(samples), requirements.sampleRate);
    }

    srt::core::Expected<std::vector<AudioPreprocessor::Slice>>
    AudioPreprocessor::prepare(
        const srt::audio::AudioBuffer &buffer,
        int sampleRate,
        const AudioRequirements &requirements,
        const srt::audio::Slicer &slicer) {

        // 1. Resample to mono float at the model-required sample rate.
        auto monoExp = resampleToMono(buffer, sampleRate, requirements);
        if (!monoExp) {
            return monoExp.takeError();
        }

        auto [audio, outSampleRate] = monoExp.take();
        (void)outSampleRate;  // == requirements.sampleRate

        // 2. Slice the mono PCM using the provided RMS slicer.
        auto markers = slicer.slice(audio);

        // 3. Build Slice list from markers.
        std::vector<Slice> slices;
        slices.reserve(markers.size());
        for (const auto &marker : markers) {
            const auto startFrame = marker.first;
            const auto endFrame = marker.second;

            Slice slice;
            slice.startFrame = startFrame;
            slice.endFrame = endFrame;
            if (endFrame > startFrame && endFrame <= static_cast<int64_t>(audio.size())) {
                slice.samples.assign(
                    audio.begin() + startFrame,
                    audio.begin() + endFrame);
            }
            slices.push_back(std::move(slice));
        }

        return slices;
    }

} // namespace srt::extract
