// lib/Audio SwresampleResampler edge condition test cases (AU-010 ~ AU-016)
//
// Covers srt::audio::SwresampleResampler::convert error paths and success
// paths. See task T-14 in docs/refactoring/06-inference-lite-optimization/README.md.
// Corresponds to the implementation in lib/Audio/src/SwresampleResampler.cpp.
//
// === Test matrix mapping ===
//   AU-010 (P0): empty input -> returns empty buffer (early return at L147-151)
//   AU-011 (P0): inputSampleRate=0 -> AudioResampleFailed (init guard at L62-69)
//   AU-012 (P0): inputSampleRate<0 -> AudioResampleFailed (same init guard)
//   AU-013 (P0): zero channel count -> AudioResampleFailed (same init guard)
//   AU-014 (P0): Unknown sample format -> AudioResampleFailed (same init guard)
//   AU-015 (P1): same config (no change) -> returns clone (init same=true at L72-81)
//   AU-016 (P1): valid resampling 44100->22050 -> succeeds with correct frame count
//
// === Implementation notes ===
// SwresampleResampler::convert first checks input.empty() (L147) and returns
// an empty buffer with the target config without initializing the swr context.
// For non-empty input, Impl::init validates parameters (L62-69):
//   srcCh>0 && dstCh>0 && srcRate>0 && dstRate>0 && srcFmt!=NONE && dstFmt!=NONE
// If any check fails, returns AudioResampleFailed. If all params match (same=true),
// init returns without creating a swr context; convert then returns input.clone().
// Otherwise, a swr context is created and the actual resampling is performed.

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/ResampleConfig.h>
#include <synthrt/Audio/SampleFormat.h>
#include <synthrt/Audio/SwresampleResampler.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

using namespace srt::audio;
using srt::core::Error;
using srt::core::ErrorCode;
using srt::core::Expected;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

// ---------------------------------------------------------------------------
// AU-010: convert with empty input returns empty buffer
// convert() checks input.empty() first (SwresampleResampler.cpp:147-151) and
// returns an empty AudioBuffer with the target config without calling init().
// ---------------------------------------------------------------------------
TEST_CASE("AU-010: convert with empty input returns empty buffer",
          "[audio][resample][edge]") {
    SwresampleResampler resampler;
    auto input = AudioBuffer::create(0, 1, SampleFormat::Float32);
    REQUIRE(input.empty());

    auto config = ResampleConfig::forMonoFloat(22050);
    auto result = resampler.convert(input, 44100, config);
    REQUIRE(result.hasValue());
    REQUIRE(result->empty());
    REQUIRE(result->frameCount() == 0);
    // Channel count and format follow the target config
    REQUIRE(result->channelCount() == 1);
    REQUIRE(result->format() == SampleFormat::Float32);
}

// ---------------------------------------------------------------------------
// AU-011: convert with inputSampleRate=0 returns AudioResampleFailed
// init() validates srcRate > 0 (L62-69). A zero source sample rate causes
// division by zero in swresample and is rejected with AudioResampleFailed.
// ---------------------------------------------------------------------------
TEST_CASE("AU-011: convert with zero inputSampleRate returns error",
          "[audio][resample][edge]") {
    SwresampleResampler resampler;
    auto input = AudioBuffer::create(100, 1, SampleFormat::Float32);
    auto config = ResampleConfig::forMonoFloat(22050);

    auto result = resampler.convert(input, 0, config);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::AudioResampleFailed);
    REQUIRE_FALSE(result.error().message().empty());
}

// ---------------------------------------------------------------------------
// AU-012: convert with negative inputSampleRate returns AudioResampleFailed
// init() validates srcRate > 0 (L62-69). A negative source sample rate is
// rejected with AudioResampleFailed.
// ---------------------------------------------------------------------------
TEST_CASE("AU-012: convert with negative inputSampleRate returns error",
          "[audio][resample][edge]") {
    SwresampleResampler resampler;
    auto input = AudioBuffer::create(100, 1, SampleFormat::Float32);
    auto config = ResampleConfig::forMonoFloat(22050);

    auto result = resampler.convert(input, -1, config);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::AudioResampleFailed);
    REQUIRE_FALSE(result.error().message().empty());
}

// ---------------------------------------------------------------------------
// AU-013: convert with zero channel count returns AudioResampleFailed
// init() validates srcCh > 0 (L62-69). A buffer with 0 channels is rejected.
// Note: AudioBuffer::create(100, 0, Float32) produces a buffer with
// channelCount=0 and byteSize=0 (degenerate but non-empty by frameCount).
// ---------------------------------------------------------------------------
TEST_CASE("AU-013: convert with zero channel count returns error",
          "[audio][resample][edge]") {
    SwresampleResampler resampler;
    // Create a buffer with 0 channels: frameCount=100 but byteSize=0.
    // input.empty() checks frameCount==0, so this buffer is NOT considered
    // empty by the early-return guard; init() is reached and validates srcCh.
    auto input = AudioBuffer::create(100, 0, SampleFormat::Float32);
    REQUIRE_FALSE(input.empty()); // frameCount=100 != 0
    REQUIRE(input.channelCount() == 0);

    auto config = ResampleConfig::forMonoFloat(22050);
    auto result = resampler.convert(input, 44100, config);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::AudioResampleFailed);
    REQUIRE_FALSE(result.error().message().empty());
}

// ---------------------------------------------------------------------------
// AU-014: convert with Unknown sample format returns AudioResampleFailed
// init() validates srcFmt != AV_SAMPLE_FMT_NONE (L62-69). An Unknown format
// maps to AV_SAMPLE_FMT_NONE via toAVSampleFormat and is rejected.
// ---------------------------------------------------------------------------
TEST_CASE("AU-014: convert with Unknown format returns error",
          "[audio][resample][edge]") {
    SwresampleResampler resampler;
    // Create a buffer with Unknown format. frameCount=100 != 0 so the
    // empty-guard is not triggered; init() validates srcFmt.
    auto input = AudioBuffer::create(100, 1, SampleFormat::Unknown);
    REQUIRE(input.format() == SampleFormat::Unknown);

    auto config = ResampleConfig::forMonoFloat(22050);
    auto result = resampler.convert(input, 44100, config);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::AudioResampleFailed);
    REQUIRE_FALSE(result.error().message().empty());
}

// ---------------------------------------------------------------------------
// AU-015: convert with same config (no change needed) returns clone
// When source and target formats/rates/channels all match (same=true at
// L72-81), init() returns without creating a swr context. convert() then
// returns input.clone() (L159-161). The output should be a copy of the input.
// ---------------------------------------------------------------------------
TEST_CASE("AU-015: convert with same config returns clone",
          "[audio][resample][edge]") {
    SwresampleResampler resampler;
    auto input = AudioBuffer::create(100, 1, SampleFormat::Float32);
    // Fill with distinguishable data
    auto span = input.floats();
    for (size_t i = 0; i < span.size(); ++i) {
        span[i] = static_cast<float>(i) * 0.01f;
    }

    // Default config: targetSampleRate=0 (keep), targetChannelCount=0 (keep),
    // targetFormat=Unknown (keep). All params match -> same=true -> clone.
    ResampleConfig config;
    auto result = resampler.convert(input, 44100, config);
    REQUIRE(result.hasValue());
    REQUIRE(result->frameCount() == 100);
    REQUIRE(result->channelCount() == 1);
    REQUIRE(result->format() == SampleFormat::Float32);
    // Verify data is preserved (clone)
    auto outSpan = result->floats();
    REQUIRE(outSpan.size() == span.size());
    for (size_t i = 0; i < outSpan.size(); ++i) {
        REQUIRE(outSpan[i] == span[i]);
    }
    // Clone produces an owned buffer, not a view
    REQUIRE_FALSE(result->isView());
}

// ---------------------------------------------------------------------------
// AU-016: convert with valid resampling (44100->22050) succeeds
// Creates a 1-second mono Float32 signal at 44100 Hz, resamples to 22050 Hz.
// The output should have approximately 22050 frames (within 10% tolerance for
// swresample's internal delay/flush behavior).
// ---------------------------------------------------------------------------
TEST_CASE("AU-016: convert with valid resampling succeeds",
          "[audio][resample][edge]") {
    SwresampleResampler resampler;
    const int64_t inputFrames = 44100;
    auto input = AudioBuffer::create(inputFrames, 1, SampleFormat::Float32);
    // Fill with a simple constant signal (avoids platform-specific M_PI issues)
    auto span = input.floats();
    for (auto &s : span) {
        s = 0.5f;
    }

    auto config = ResampleConfig::forMonoFloat(22050);
    auto result = resampler.convert(input, 44100, config);
    REQUIRE(result.hasValue());
    REQUIRE(result->frameCount() > 0);
    REQUIRE(result->channelCount() == 1);
    REQUIRE(result->format() == SampleFormat::Float32);

    // Output frame count should be approximately 22050 (half of 44100).
    // Allow 10% tolerance for swresample's internal delay/flush behavior.
    const int64_t expectedFrames = 22050;
    const double tolerance = expectedFrames * 0.10;
    REQUIRE(result->frameCount() >= static_cast<int64_t>(expectedFrames - tolerance));
    REQUIRE(result->frameCount() <= static_cast<int64_t>(expectedFrames + tolerance));
}
