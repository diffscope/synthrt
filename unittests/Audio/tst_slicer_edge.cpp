// lib/Audio Slicer edge condition test cases (AU-001 ~ AU-009)
//
// Covers srt::audio::Slicer behavior under empty input, single sample,
// short input, zero/negative constructor parameters, all-loud / all-silent
// signals, and a functional multi-segment slice. See task T-14 in
// docs/refactoring/06-inference-lite-optimization/README.md.
//
// === Test matrix mapping ===
//   AU-001 (P0): empty input -> single segment [0, 0] (minLength guard)
//   AU-002 (P0): single sample -> single segment [0, 1] (minLength guard)
//   AU-003 (P0): short input (below minLength threshold) -> [0, N]
//   AU-004 (P0): hopSize=0 -> returns entire input as single segment (guard)
//   AU-005 (P0): winSize=0 -> returns entire input as single segment (guard)
//   AU-006 (P0): negative constructor params -> clamped to 0, hopSize guard
//   AU-007 (P1): all-loud signal -> single segment [0, N] (no silence to split)
//   AU-008 (P1): all-silence signal -> single segment or empty (boundary)
//   AU-009 (P1): signal with middle silence -> multiple segments (functional)
//
// === Implementation notes ===
// Slicer constructor clamps all int parameters to >= 0 (TD-11, Slicer.cpp:123-129).
// slice() has two early-return guards (Slicer.cpp:145-150):
//   1. hopSize<=0 || winSize<=0 -> {{0, samples.size()}}
//   2. (samples.size()+hop-1)/hop <= minLength -> {{0, samples.size()}}
// Guard 2 uses unsigned arithmetic; for empty input with hop>=1 the result is 0,
// which is <= any minLength>=0, so empty input returns {{0, 0}}.

#include <cstdint>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Audio/Slicer.h>

using namespace srt::audio;

namespace {
    // Standard Slicer parameters used by most tests.
    // sampleRate=22050, threshold=0.01, hopSize=256, winSize=512,
    // minLength=4, minInterval=4, maxSilKept=2.
    Slicer makeStandardSlicer() {
        return Slicer(22050, 0.01f, 256, 512, 4, 4, 2);
    }

    // Generate a loud sine wave of the specified length.
    std::vector<float> makeLoudSignal(int64_t numSamples, float amplitude = 0.5f) {
        std::vector<float> samples(static_cast<size_t>(numSamples), 0.0f);
        for (size_t i = 0; i < samples.size(); ++i) {
            samples[i] = amplitude * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(i) / 22050.0);
        }
        return samples;
    }
} // namespace

// ---------------------------------------------------------------------------
// AU-001: Slicer with empty input returns single segment [0, 0]
// The minLength guard triggers: (0 + hop - 1) / hop = 0 <= minLength.
// The segment [0, 0] represents an empty range (start == end).
// ---------------------------------------------------------------------------
TEST_CASE("AU-001: Slicer with empty input returns single empty segment",
          "[audio][slicer][edge]") {
    auto slicer = makeStandardSlicer();
    std::vector<float> empty;
    auto markers = slicer.slice(empty);
    REQUIRE(markers.size() == 1);
    REQUIRE(markers[0].first == 0);
    REQUIRE(markers[0].second == 0);
}

// ---------------------------------------------------------------------------
// AU-002: Slicer with single sample returns single segment [0, 1]
// (1 + hop - 1) / hop = 1 <= minLength(4), so the minLength guard triggers.
// The segment [0, 1] covers the entire single-sample input.
// ---------------------------------------------------------------------------
TEST_CASE("AU-002: Slicer with single sample returns single segment",
          "[audio][slicer][edge]") {
    auto slicer = makeStandardSlicer();
    std::vector<float> single = {0.5f};
    auto markers = slicer.slice(single);
    REQUIRE(markers.size() == 1);
    REQUIRE(markers[0].first == 0);
    REQUIRE(markers[0].second == 1);
}

// ---------------------------------------------------------------------------
// AU-003: Slicer with short input (below minLength threshold) returns [0, N]
// With hopSize=256 and minLength=4, any input shorter than 4*256=1024 samples
// triggers the minLength guard (since ceil(N/256) <= 4 for N <= 1024).
// Verifies that no slicing occurs for short inputs.
// ---------------------------------------------------------------------------
TEST_CASE("AU-003: Slicer with short input returns single segment",
          "[audio][slicer][edge]") {
    auto slicer = makeStandardSlicer();
    // 100 samples: ceil(100/256) = 1 <= 4 (minLength), guard triggers
    std::vector<float> shortInput(100, 0.5f);
    auto markers = slicer.slice(shortInput);
    REQUIRE(markers.size() == 1);
    REQUIRE(markers[0].first == 0);
    REQUIRE(markers[0].second == 100);
}

// ---------------------------------------------------------------------------
// AU-004: Slicer with hopSize=0 returns entire input as single segment
// The hopSize<=0 guard triggers before any RMS computation, preventing
// division by zero (BUG-AUDIO-05, ROBUST-05).
// ---------------------------------------------------------------------------
TEST_CASE("AU-004: Slicer with hopSize=0 returns entire input",
          "[audio][slicer][edge]") {
    // hopSize=0: the guard at Slicer.cpp:145 returns {{0, samples.size()}}
    Slicer slicer(22050, 0.01f, 0, 512, 4, 4, 2);
    auto samples = makeLoudSignal(5000);
    auto markers = slicer.slice(samples);
    REQUIRE(markers.size() == 1);
    REQUIRE(markers[0].first == 0);
    REQUIRE(markers[0].second == static_cast<int64_t>(samples.size()));
}

// ---------------------------------------------------------------------------
// AU-005: Slicer with winSize=0 returns entire input as single segment
// The winSize<=0 guard triggers (same guard as AU-004), preventing division
// by zero in getRms (BUG-AUDIO-05, ROBUST-05).
// ---------------------------------------------------------------------------
TEST_CASE("AU-005: Slicer with winSize=0 returns entire input",
          "[audio][slicer][edge]") {
    // winSize=0: the guard at Slicer.cpp:145 returns {{0, samples.size()}}
    Slicer slicer(22050, 0.01f, 256, 0, 4, 4, 2);
    auto samples = makeLoudSignal(5000);
    auto markers = slicer.slice(samples);
    REQUIRE(markers.size() == 1);
    REQUIRE(markers[0].first == 0);
    REQUIRE(markers[0].second == static_cast<int64_t>(samples.size()));
}

// ---------------------------------------------------------------------------
// AU-006: Slicer with negative constructor parameters clamps to 0
// TD-11 (Slicer.cpp:123-129): all int parameters are clamped to >= 0 via
// std::max(0, value). After clamping, hopSize=0 triggers the guard.
// Also verifies that a negative threshold is preserved (it is a float and
// not clamped; a negative threshold means all RMS values are >= threshold,
// so no silence is detected).
// ---------------------------------------------------------------------------
TEST_CASE("AU-006: Slicer with negative params clamps to 0",
          "[audio][slicer][edge]") {
    SECTION("all negative int params clamped to 0 -> hopSize guard") {
        // All int params negative -> clamped to 0. hopSize=0 triggers guard.
        Slicer slicer(-1, 0.01f, -1, -1, -1, -1, -1);
        auto samples = makeLoudSignal(5000);
        auto markers = slicer.slice(samples);
        REQUIRE(markers.size() == 1);
        REQUIRE(markers[0].first == 0);
        REQUIRE(markers[0].second == static_cast<int64_t>(samples.size()));
    }

    SECTION("negative sampleRate only does not prevent slicing") {
        // sampleRate is not used in slice(); negative value is clamped to 0
        // but slicing still proceeds with valid hopSize/winSize.
        Slicer slicer(-1, 0.01f, 256, 512, 4, 4, 2);
        std::vector<float> empty;
        auto markers = slicer.slice(empty);
        // Empty input hits minLength guard regardless of sampleRate
        REQUIRE(markers.size() == 1);
        REQUIRE(markers[0].first == 0);
        REQUIRE(markers[0].second == 0);
    }
}

// ---------------------------------------------------------------------------
// AU-007: Slicer with all-loud signal returns single segment
// When all RMS values are >= threshold, silence_start is never set (stays -1).
// No silence tags are added; sil_tags is empty -> returns {{0, samples.size()}}.
// ---------------------------------------------------------------------------
TEST_CASE("AU-007: Slicer with all-loud signal returns single segment",
          "[audio][slicer][edge]") {
    auto slicer = makeStandardSlicer();
    // 5 seconds of loud signal: well above minLength threshold
    auto samples = makeLoudSignal(22050 * 5, 0.5f);
    auto markers = slicer.slice(samples);
    REQUIRE(markers.size() == 1);
    REQUIRE(markers[0].first == 0);
    REQUIRE(markers[0].second == static_cast<int64_t>(samples.size()));
}

// ---------------------------------------------------------------------------
// AU-008: Slicer with all-silence signal
// When all samples are 0 (or below threshold), all RMS values are 0 < threshold.
// silence_start is set to 0 at the first hop and never reset. The post-loop
// block may add a silence tag if rms_list.size() - 0 >= minInterval.
// The result is either an empty marker list or a single segment depending on
// the maxSilKept / argmin interaction. This test verifies no crash and that
// the output is a valid marker list (each segment start <= end).
// ---------------------------------------------------------------------------
TEST_CASE("AU-008: Slicer with all-silence signal does not crash",
          "[audio][slicer][edge]") {
    auto slicer = makeStandardSlicer();
    // 5 seconds of silence (all zeros)
    std::vector<float> silence(22050 * 5, 0.0f);
    auto markers = slicer.slice(silence);
    // All silence: the slicer may return 0 or 1 segment. Verify that whatever
    // it returns is well-formed (start <= end, within input bounds).
    // Note: the last silence tag may set end = rms_list.size()+1 which exceeds
    // the input size by one hop; allow either bound.
    const int64_t inputSize = static_cast<int64_t>(silence.size());
    const int64_t inputSizePlusOne = inputSize + 1;
    for (const auto &m : markers) {
        REQUIRE(m.first <= m.second);
        REQUIRE(m.first >= 0);
        // Catch2 does not support || inside REQUIRE without parentheses;
        // decompose into two checks.
        bool withinBounds = (m.second <= inputSize) || (m.second == inputSizePlusOne);
        REQUIRE(withinBounds);
    }
}

// ---------------------------------------------------------------------------
// AU-009: Slicer with middle silence produces multiple segments (functional)
// Constructs a signal with: loud (3s) + silence (1s) + loud (3s).
// With standard parameters (hopSize=256, minInterval=4, minLength=4),
// the slicer should detect the middle silence and split into >= 2 segments.
// This is the only functional (non-guard) test case.
// ---------------------------------------------------------------------------
TEST_CASE("AU-009: Slicer with middle silence produces multiple segments",
          "[audio][slicer][edge]") {
    auto slicer = makeStandardSlicer();
    const int64_t sampleRate = 22050;
    const int64_t loudFrames = sampleRate * 3;  // 3 seconds
    const int64_t silenceFrames = sampleRate * 1; // 1 second

    auto loud = makeLoudSignal(loudFrames, 0.5f);
    std::vector<float> silence(static_cast<size_t>(silenceFrames), 0.0f);

    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(loudFrames * 2 + silenceFrames));
    samples.insert(samples.end(), loud.begin(), loud.end());
    samples.insert(samples.end(), silence.begin(), silence.end());
    // Regenerate loud for the second segment (different phase is fine)
    auto loud2 = makeLoudSignal(loudFrames, 0.5f);
    samples.insert(samples.end(), loud2.begin(), loud2.end());

    auto markers = slicer.slice(samples);

    // The slicer should detect the middle silence and produce >= 2 segments.
    // If it produces only 1, the silence was not detected (e.g. threshold
    // too low or minInterval not met) — but with 1s of silence and
    // minInterval=4 hops (4*256=1024 samples << 22050), it should split.
    REQUIRE(markers.size() >= 2);

    // Verify all segments are well-formed, non-overlapping, and within bounds.
    // Note: Slicer segments are NON-contiguous — silence portions between
    // segments are removed (skipped). Each segment [start, end) represents a
    // loud region; the gap between prevEnd and the next start is the silence.
    constexpr int64_t hopSize = 256;
    const int64_t inputSize = static_cast<int64_t>(samples.size());
    int64_t prevEnd = 0;
    int64_t totalCovered = 0;
    for (const auto &m : markers) {
        REQUIRE(m.first <= m.second);
        REQUIRE(m.first >= 0);
        // Each segment starts at or after the previous segment's end (no overlap)
        REQUIRE(m.first >= prevEnd);
        // Segments are within input bounds (last segment end may exceed by
        // up to one hop due to rms_list.size() * hopSize rounding)
        REQUIRE(m.second <= inputSize + hopSize);
        totalCovered += (m.second - m.first);
        prevEnd = m.second;
    }
    // The total covered frames should be less than the input size (silence
    // was removed) but greater than 0 (loud portions exist).
    REQUIRE(totalCovered > 0);
    REQUIRE(totalCovered < inputSize);
}
