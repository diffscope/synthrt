// Extended algorithm tests simulating real ds-editor-lite call patterns.
//
// These tests mirror the actual usage in the 5 DiffSinger inference plugins:
//   - resample(param.values, param.interval, frameWidth, targetLength, true)
//   - frameWidth = hopSize / sampleRate (e.g. 512/44100 ≈ 0.0116, or 0.01)
//   - param.interval typically 0.01 (10ms) for pitch curves
//   - targetLength = totalDuration / frameWidth
//   - fillRestMidi used in PitchInference with note.key + note.is_rest
//   - interpolate used internally by resample

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <inferutil/Algorithm.h>

using namespace ds::infer::inferutil;

namespace {
    bool approxEqual(double a, double b, double eps = 1e-6) {
        return std::abs(a - b) < eps;
    }

    // Helper: simulate PitchInference parameter resampling
    // param.values at param.interval -> resampled to frameWidth grid, targetLength frames
    std::vector<double> resampleParam(const std::vector<double> &values, double paramInterval,
                                       double frameWidth, int64_t targetLength, bool fillLast) {
        auto sv = stdc::array_view<double>(const_cast<std::vector<double> &>(values));
        return resample(sv, paramInterval, frameWidth, targetLength, fillLast);
    }
}

// ---------------------------------------------------------------------------
// Real-world resample scenarios: pitch/parameter curves
// ---------------------------------------------------------------------------

TEST_CASE("resample pitch curve 10ms to frameWidth 10ms same grid", "[algorithm][realworld]") {
    // Real scenario: param.interval=0.01, frameWidth=0.01 (Duration model config)
    // 100 samples at 10ms: tMax = 99*0.01 = 0.99s
    // arange(0, 0.99, 0.01) = 99 elements, then fillLast=true pads to 100
    std::vector<double> pitch;
    for (int i = 0; i < 100; ++i)
        pitch.push_back(440.0 + i * 0.1);
    auto result = resampleParam(pitch, 0.01, 0.01, 100, true);
    REQUIRE(result.size() == 100);
    REQUIRE(approxEqual(result[0], 440.0));
    // Index 98 is the last interpolated value (440 + 98*0.1)
    // Index 99 is tail-filled with result[98]
    REQUIRE(approxEqual(result[98], 440.0 + 98 * 0.1, 0.5));
    REQUIRE(approxEqual(result[99], result[98]));
}

TEST_CASE("resample pitch curve 10ms to acoustic frameWidth 512/44100", "[algorithm][realworld]") {
    // Real scenario: param.interval=0.01 (pitch curve), frameWidth=512/44100≈0.0116
    // totalDuration = 100 * 0.01 = 1.0s, targetLength = lround(1.0 / 0.0116) = 86
    double frameWidth = 512.0 / 44100.0;
    int64_t targetLength = static_cast<int64_t>(std::llround(1.0 / frameWidth));
    std::vector<double> pitch(100, 440.0);
    auto result = resampleParam(pitch, 0.01, frameWidth, targetLength, true);
    REQUIRE(result.size() == static_cast<size_t>(targetLength));
    // All values should be approximately 440 (constant pitch)
    for (auto v : result) {
        REQUIRE(approxEqual(v, 440.0, 1.0));
    }
}

TEST_CASE("resample linear ramp preserves slope", "[algorithm][realworld]") {
    // Linear pitch ramp from 0 to 98 over 50 samples at 10ms
    // tMax = 49*0.01 = 0.49s, arange(0, 0.49, 0.01) = 49 elements
    // targetLength=50, so 1 tail fill with last value
    std::vector<double> ramp;
    for (int i = 0; i < 50; ++i)
        ramp.push_back(i * 2.0);
    auto result = resampleParam(ramp, 0.01, 0.01, 50, true);
    REQUIRE(result.size() == 50);
    // Indices 0-48 are interpolated, index 49 is tail-filled
    for (size_t i = 0; i < 49; ++i) {
        REQUIRE(approxEqual(result[i], static_cast<double>(i) * 2.0, 0.1));
    }
    // Tail fill: result[49] == result[48]
    REQUIRE(approxEqual(result[49], result[48]));
}

TEST_CASE("resample downsampling from 10ms to 20ms", "[algorithm][realworld]") {
    // Downsample: 100 samples at 10ms -> targetLength=50 at 20ms frameWidth
    std::vector<double> values;
    for (int i = 0; i < 100; ++i)
        values.push_back(static_cast<double>(i));
    auto result = resampleParam(values, 0.01, 0.02, 50, true);
    REQUIRE(result.size() == 50);
    // First value should be 0, approximately
    REQUIRE(approxEqual(result[0], 0.0, 0.5));
}

TEST_CASE("resample upsampling from 20ms to 10ms with tail fill", "[algorithm][realworld]") {
    // Upsample: 50 samples at 20ms (total 1.0s) -> targetLength=100 at 10ms
    // But actual interpolated length = tMax/targetTimestep = (49*0.02)/0.01 = 98
    // So 2 tail frames should be filled with last value
    std::vector<double> values(50, 42.0);
    auto result = resampleParam(values, 0.02, 0.01, 100, true);
    REQUIRE(result.size() == 100);
    // Tail (indices 98-99) should be filled with 42.0
    REQUIRE(approxEqual(result[98], 42.0));
    REQUIRE(approxEqual(result[99], 42.0));
}

TEST_CASE("resample tone_shift with fillLast=false zeros tail", "[algorithm][realworld]") {
    // AcousticInference uses fillLast=false for tone_shift
    std::vector<double> toneShift(50, 5.0);
    auto result = resampleParam(toneShift, 0.02, 0.01, 100, false);
    REQUIRE(result.size() == 100);
    // Tail should be 0 (fillLast=false)
    REQUIRE(approxEqual(result[99], 0.0));
}

TEST_CASE("resample empty param values returns empty (optional param)", "[algorithm][realworld]") {
    // AcousticInference: some params are optional (empty values -> resample returns empty)
    std::vector<double> empty;
    auto result = resampleParam(empty, 0.01, 0.01, 100, true);
    REQUIRE(result.empty());
}

TEST_CASE("resample single point param broadcasts", "[algorithm][realworld]") {
    // Single-value parameter curve (static value)
    std::vector<double> single{0.5};
    auto result = resampleParam(single, 0.01, 0.01, 200, true);
    REQUIRE(result.size() == 200);
    for (auto v : result) {
        REQUIRE(approxEqual(v, 0.5));
    }
}

// ---------------------------------------------------------------------------
// Real-world fillRestMidi scenarios from PitchInference
// ---------------------------------------------------------------------------

TEST_CASE("fillRestMidi realistic note sequence with rest in middle", "[algorithm][fillrest][realworld]") {
    // Simulate: C4(60), rest, rest, E4(64), F4(65), rest, G4(67)
    std::vector<double> midi{60.0, 0.0, 0.0, 64.0, 65.0, 0.0, 67.0};
    std::vector<uint8_t> isRest{0, 1, 1, 0, 0, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // Rest at index 1-2: left=60, right=64, mid=1+(2+1)/2=2
    // index 1 -> 60, index 2 -> 64
    REQUIRE(midi[1] == 60.0);
    REQUIRE(midi[2] == 64.0);
    // Rest at index 5: single element between 65 and 67
    // mid = 5 + 1 = 6 = end, so first loop fills left (65)
    REQUIRE(midi[5] == 65.0);
    // Non-rest values unchanged
    REQUIRE(midi[0] == 60.0);
    REQUIRE(midi[3] == 64.0);
    REQUIRE(midi[4] == 65.0);
    REQUIRE(midi[6] == 67.0);
}

TEST_CASE("fillRestMidi all rests except one note", "[algorithm][fillrest][realworld]") {
    // Only one non-rest note: all rests fill from that note
    std::vector<double> midi{0.0, 0.0, 0.0, 60.0, 0.0, 0.0};
    std::vector<uint8_t> isRest{1, 1, 1, 0, 1, 1};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // Leading rests (0-2): fill with right neighbor (60)
    REQUIRE(midi[0] == 60.0);
    REQUIRE(midi[1] == 60.0);
    REQUIRE(midi[2] == 60.0);
    // Trailing rests (4-5): fill with left neighbor (60)
    REQUIRE(midi[4] == 60.0);
    REQUIRE(midi[5] == 60.0);
    // Non-rest unchanged
    REQUIRE(midi[3] == 60.0);
}

TEST_CASE("fillRestMidi alternating rest and note", "[algorithm][fillrest][realworld]") {
    // Pattern: note, rest, note, rest, note
    std::vector<double> midi{60.0, 0.0, 62.0, 0.0, 64.0};
    std::vector<uint8_t> isRest{0, 1, 0, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // Each single rest between two notes: mid = start+1 = end, fills left
    REQUIRE(midi[1] == 60.0); // left neighbor
    REQUIRE(midi[3] == 62.0); // left neighbor
}

TEST_CASE("fillRestMidi large rest region splits evenly", "[algorithm][fillrest][realworld]") {
    // 10 rest notes between two notes
    std::vector<double> midi(12, 0.0);
    midi[0] = 60.0;
    midi[11] = 72.0;
    std::vector<uint8_t> isRest{0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // mid = 1 + (10+1)/2 = 1 + 5 = 6
    // indices 1-5 -> 60, indices 6-10 -> 72
    for (int i = 1; i <= 5; ++i)
        REQUIRE(midi[i] == 60.0);
    for (int i = 6; i <= 10; ++i)
        REQUIRE(midi[i] == 72.0);
}

TEST_CASE("fillRestMidi odd rest region size", "[algorithm][fillrest][realworld]") {
    // 3 rest notes between two notes
    std::vector<double> midi{60.0, 0.0, 0.0, 0.0, 70.0};
    std::vector<uint8_t> isRest{0, 1, 1, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // mid = 1 + (3+1)/2 = 1 + 2 = 3
    // indices 1-2 -> 60, index 3 -> 70
    REQUIRE(midi[1] == 60.0);
    REQUIRE(midi[2] == 60.0);
    REQUIRE(midi[3] == 70.0);
}

TEST_CASE("fillRestMidi with integer type", "[algorithm][fillrest][realworld]") {
    // PitchInference uses float, but some paths may use int
    std::vector<int> midi{60, 0, 0, 64};
    std::vector<uint8_t> isRest{0, 1, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // mid = 1 + (2+1)/2 = 1 + 1 = 2
    // index 1 -> 60, index 2 -> 64
    REQUIRE(midi[1] == 60);
    REQUIRE(midi[2] == 64);
}

// ---------------------------------------------------------------------------
// Real-world interpolate scenarios
// ---------------------------------------------------------------------------

TEST_CASE("interpolate multiple sample points linear", "[algorithm][interpolate][realworld]") {
    // Resample 5 points to 9 points (upsample by ~2x)
    std::vector<double> refPoints{0.0, 1.0, 2.0, 3.0, 4.0};
    std::vector<double> refValues{0.0, 10.0, 20.0, 30.0, 40.0};
    std::vector<double> samplePoints{0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 9);
    for (size_t i = 0; i < result.size(); ++i) {
        REQUIRE(approxEqual(result[i], static_cast<double>(i) * 5.0));
    }
}

TEST_CASE("interpolate nearest neighbor method", "[algorithm][interpolate][realworld]") {
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues{10.0, 20.0};
    std::vector<double> samplePoints{0.3, 0.6, 0.7};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate<InterpolateNearestNeighbor, double>(sp, rp, rv);
    REQUIRE(result.size() == 3);
    // 0.3: (0.3-0) > (1-0.3)? 0.3 > 0.7? no -> 10
    REQUIRE(approxEqual(result[0], 10.0));
    // 0.6: 0.6 > 0.4? yes -> 20
    REQUIRE(approxEqual(result[1], 20.0));
    // 0.7: 0.7 > 0.3? yes -> 20
    REQUIRE(approxEqual(result[2], 20.0));
}

TEST_CASE("interpolate nearest neighbor up method", "[algorithm][interpolate][realworld]") {
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues{10.0, 20.0};
    std::vector<double> samplePoints{0.5}; // exactly midpoint
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate<InterpolateNearestNeighborUp, double>(sp, rp, rv);
    // 0.5: (0.5-0) >= (1-0.5)? 0.5 >= 0.5? yes -> 20 (up rounds to right)
    REQUIRE(approxEqual(result[0], 20.0));
}

TEST_CASE("interpolate NaN fill for out-of-range by default", "[algorithm][interpolate][realworld]") {
    // Default left/right fill is NaN
    std::vector<double> refPoints{1.0, 2.0};
    std::vector<double> refValues{10.0, 20.0};
    std::vector<double> samplePoints{0.5, 2.5}; // before front, after back
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 2);
    REQUIRE(std::isnan(result[0]));
    REQUIRE(std::isnan(result[1]));
}

TEST_CASE("interpolate at exact reference points returns exact values", "[algorithm][interpolate][realworld]") {
    std::vector<double> refPoints{0.0, 0.5, 1.0, 1.5, 2.0};
    std::vector<double> refValues{100.0, 50.0, 0.0, -50.0, -100.0};
    std::vector<double> samplePoints{0.0, 0.5, 1.0, 1.5, 2.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 5);
    REQUIRE(approxEqual(result[0], 100.0));
    REQUIRE(approxEqual(result[1], 50.0));
    REQUIRE(approxEqual(result[2], 0.0));
    REQUIRE(approxEqual(result[3], -50.0));
    REQUIRE(approxEqual(result[4], -100.0));
}

// ---------------------------------------------------------------------------
// Real-world arange scenarios used internally by resample
// ---------------------------------------------------------------------------

TEST_CASE("arange used in resample time axis construction", "[algorithm][arange][realworld]") {
    // resample internally: arange(0.0, tMax, targetTimestep)
    // where tMax = (samples.size()-1) * timestep
    // For 100 samples at 0.01s: tMax = 0.99s
    // targetTimestep = 0.01 (same grid): arange(0.0, 0.99, 0.01) -> 99 elements
    auto timeAxis = arange(0.0, 99.0 * 0.01, 0.01);
    REQUIRE(timeAxis.size() == 99);
    REQUIRE(approxEqual(timeAxis[0], 0.0));
    REQUIRE(approxEqual(timeAxis[98], 98 * 0.01));
}

TEST_CASE("arange with very small step produces large array", "[algorithm][arange][realworld]") {
    // 10 seconds at 1ms resolution
    auto result = arange(0.0, 10.0, 0.001);
    REQUIRE(result.size() == 10000);
    REQUIRE(approxEqual(result[0], 0.0));
    REQUIRE(approxEqual(result[9999], 9.999));
}

TEST_CASE("arange negative step for descending", "[algorithm][arange][realworld]") {
    auto result = arange(5.0, 0.0, -1.0);
    REQUIRE(result.size() == 5);
    REQUIRE(result[0] == 5.0);
    REQUIRE(result[4] == 1.0);
}

TEST_CASE("arange start equals stop returns empty", "[algorithm][arange][realworld]") {
    auto result = arange(5.0, 5.0, 1.0);
    // ceil((5-5)/1) = 0 -> empty
    REQUIRE(result.empty());
}
