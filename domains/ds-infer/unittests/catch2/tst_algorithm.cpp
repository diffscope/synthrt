// Unit tests for ds::infer::inferutil algorithm helpers.
//
// Covers arange(), interpolate(), resample(), and fillRestMidiWithNearestInPlace()
// edge cases including empty inputs, single-element inputs, division-by-zero
// guards, and boundary fill behavior.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <inferutil/Algorithm.h>

using namespace ds::infer::inferutil;

namespace {
    // Simple approximate equality for double comparison (no Catch2 matchers available).
    bool approxEqual(double a, double b, double eps = 1e-9) {
        return std::abs(a - b) < eps;
    }
}

// ---------------------------------------------------------------------------
// arange
// ---------------------------------------------------------------------------

TEST_CASE("arange returns empty when step is zero", "[algorithm][arange]") {
    // BF-16 regression: step==0 must not divide by zero.
    auto result = arange(0.0, 10.0, 0.0);
    REQUIRE(result.empty());
}

TEST_CASE("arange returns empty when stop < start with positive step", "[algorithm][arange]") {
    auto result = arange(10.0, 0.0, 1.0);
    REQUIRE(result.empty());
}

TEST_CASE("arange basic ascending", "[algorithm][arange]") {
    auto result = arange(0.0, 5.0, 1.0);
    REQUIRE(result.size() == 5);
    REQUIRE(result[0] == 0.0);
    REQUIRE(result[4] == 4.0);
}

TEST_CASE("arange with fractional step", "[algorithm][arange]") {
    auto result = arange(0.0, 1.0, 0.25);
    REQUIRE(result.size() == 4);
    REQUIRE(approxEqual(result[0], 0.0));
    REQUIRE(approxEqual(result[3], 0.75));
}

TEST_CASE("arange single element when step exceeds range", "[algorithm][arange]") {
    auto result = arange(0.0, 1.0, 5.0);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 0.0);
}

TEST_CASE("arange with negative start", "[algorithm][arange]") {
    auto result = arange(-2.0, 2.0, 1.0);
    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == -2.0);
    REQUIRE(result[3] == 1.0);
}

// ---------------------------------------------------------------------------
// interpolate
// ---------------------------------------------------------------------------

TEST_CASE("interpolate empty sample points returns empty", "[algorithm][interpolate]") {
    std::vector<double> samplePoints;
    std::vector<double> refPoints{0.0, 1.0, 2.0};
    std::vector<double> refValues{10.0, 20.0, 30.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.empty());
}

TEST_CASE("interpolate exact match at reference point", "[algorithm][interpolate]") {
    std::vector<double> samplePoints{1.0};
    std::vector<double> refPoints{0.0, 1.0, 2.0};
    std::vector<double> refValues{10.0, 20.0, 30.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 1);
    REQUIRE(approxEqual(result[0], 20.0));
}

TEST_CASE("interpolate linear midpoint", "[algorithm][interpolate]") {
    std::vector<double> samplePoints{0.5};
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues{10.0, 20.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 1);
    REQUIRE(approxEqual(result[0], 15.0));
}

TEST_CASE("interpolate left fill for sample before front", "[algorithm][interpolate]") {
    std::vector<double> samplePoints{-1.0};
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues{10.0, 20.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate<InterpolateLinear, double>(
        sp, rp, rv, -999.0, -888.0);
    REQUIRE(result.size() == 1);
    REQUIRE(approxEqual(result[0], -999.0));
}

TEST_CASE("interpolate right fill for sample after back", "[algorithm][interpolate]") {
    std::vector<double> samplePoints{5.0};
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues{10.0, 20.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate<InterpolateLinear, double>(
        sp, rp, rv, -999.0, -888.0);
    REQUIRE(result.size() == 1);
    REQUIRE(approxEqual(result[0], -888.0));
}

TEST_CASE("interpolate sample exactly at front returns front value", "[algorithm][interpolate]") {
    std::vector<double> samplePoints{0.0};
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues{10.0, 20.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 1);
    REQUIRE(approxEqual(result[0], 10.0));
}

TEST_CASE("interpolate sample exactly at back returns back value", "[algorithm][interpolate]") {
    std::vector<double> samplePoints{1.0};
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues{10.0, 20.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 1);
    REQUIRE(approxEqual(result[0], 20.0));
}

// ---------------------------------------------------------------------------
// resample
// ---------------------------------------------------------------------------

TEST_CASE("resample empty samples returns empty", "[algorithm][resample]") {
    std::vector<double> samples;
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.01, 0.01, 100, true);
    REQUIRE(result.empty());
}

TEST_CASE("resample zero target length returns empty", "[algorithm][resample]") {
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.01, 0.01, 0, true);
    REQUIRE(result.empty());
}

TEST_CASE("resample single sample broadcasts to target length", "[algorithm][resample]") {
    std::vector<double> samples{42.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.01, 0.01, 5, true);
    REQUIRE(result.size() == 5);
    for (auto v : result) {
        REQUIRE(approxEqual(v, 42.0));
    }
}

TEST_CASE("resample zero timestep returns empty", "[algorithm][resample]") {
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.0, 0.01, 10, true);
    REQUIRE(result.empty());
}

TEST_CASE("resample zero target timestep returns empty", "[algorithm][resample]") {
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.01, 0.0, 10, true);
    REQUIRE(result.empty());
}

TEST_CASE("resample target length 1 returns first sample", "[algorithm][resample]") {
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.01, 0.01, 1, true);
    REQUIRE(result.size() == 1);
    REQUIRE(approxEqual(result[0], 1.0));
}

TEST_CASE("resample truncates when actual > target", "[algorithm][resample]") {
    std::vector<double> samples{1.0, 2.0, 3.0, 4.0, 5.0};
    auto sv = stdc::array_view<double>(samples);
    // Same timestep, targetLength smaller than samples
    auto result = resample(sv, 0.01, 0.01, 3, true);
    REQUIRE(result.size() == 3);
}

TEST_CASE("resample fills tail with last value when fillLast=true", "[algorithm][resample]") {
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    // Large target timestep so actual < target, requiring tail fill
    auto result = resample(sv, 0.01, 0.05, 10, true);
    REQUIRE(result.size() == 10);
    // Tail values should be the last interpolated value (fillLast=true)
    // Not zero.
    REQUIRE(result.back() != 0.0);
}

TEST_CASE("resample fills tail with zero when fillLast=false", "[algorithm][resample]") {
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.01, 0.05, 10, false);
    REQUIRE(result.size() == 10);
    // Tail values should be 0
    REQUIRE(approxEqual(result.back(), 0.0));
}

// ---------------------------------------------------------------------------
// fillRestMidiWithNearestInPlace
// ---------------------------------------------------------------------------

TEST_CASE("fillRestMidi size mismatch returns false", "[algorithm][fillrest]") {
    std::vector<double> midi{60.0, 62.0};
    std::vector<uint8_t> isRest{0}; // mismatched size
    REQUIRE(!fillRestMidiWithNearestInPlace(midi, isRest));
}

TEST_CASE("fillRestMidi empty vectors returns true", "[algorithm][fillrest]") {
    std::vector<double> midi;
    std::vector<uint8_t> isRest;
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    REQUIRE(midi.empty());
}

TEST_CASE("fillRestMidi no rests leaves midi unchanged", "[algorithm][fillrest]") {
    std::vector<double> midi{60.0, 62.0, 64.0};
    std::vector<uint8_t> isRest{0, 0, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    REQUIRE(midi[0] == 60.0);
    REQUIRE(midi[1] == 62.0);
    REQUIRE(midi[2] == 64.0);
}

TEST_CASE("fillRestMidi middle rest splits between neighbors", "[algorithm][fillrest]") {
    std::vector<double> midi{60.0, 0.0, 0.0, 70.0};
    std::vector<uint8_t> isRest{0, 1, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // First half of rest region gets left value (60), second half gets right (70)
    REQUIRE(midi[1] == 60.0);
    REQUIRE(midi[2] == 70.0);
}

TEST_CASE("fillRestMidi leading rest fills with right neighbor", "[algorithm][fillrest]") {
    std::vector<double> midi{0.0, 0.0, 60.0};
    std::vector<uint8_t> isRest{1, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    REQUIRE(midi[0] == 60.0);
    REQUIRE(midi[1] == 60.0);
}

TEST_CASE("fillRestMidi trailing rest fills with left neighbor", "[algorithm][fillrest]") {
    std::vector<double> midi{60.0, 0.0, 0.0};
    std::vector<uint8_t> isRest{0, 1, 1};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    REQUIRE(midi[1] == 60.0);
    REQUIRE(midi[2] == 60.0);
}

TEST_CASE("fillRestMidi single rest between notes gets left value", "[algorithm][fillrest]") {
    std::vector<double> midi{60.0, 0.0, 70.0};
    std::vector<uint8_t> isRest{0, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // Single-element rest: mid = start + 1 = end, so first loop fills left
    REQUIRE(midi[1] == 60.0);
}

TEST_CASE("fillRestMidi all rest returns true without crash", "[algorithm][fillrest][extreme]") {
    // Every element is rest — no non-rest value to fill from.
    // The function returns true (no error) and leaves midi unchanged.
    std::vector<double> midi{0.0, 0.0, 0.0};
    std::vector<uint8_t> isRest{1, 1, 1};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // All values should remain 0 (unchanged)
    for (auto v : midi) {
        REQUIRE(v == 0.0);
    }
}

TEST_CASE("fillRestMidi single rest element returns true", "[algorithm][fillrest][extreme]") {
    // Single element which is rest — no neighbors to fill from.
    std::vector<double> midi{0.0};
    std::vector<uint8_t> isRest{1};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    REQUIRE(midi[0] == 0.0); // unchanged
}

TEST_CASE("fillRestMidi alternating rest and note", "[algorithm][fillrest][extreme]") {
    // Alternating rest/note pattern — each single rest gets left neighbor.
    std::vector<double> midi{60.0, 0.0, 62.0, 0.0, 64.0};
    std::vector<uint8_t> isRest{0, 1, 0, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // Each rest is a single-element middle segment → gets left value
    REQUIRE(midi[1] == 60.0);
    REQUIRE(midi[3] == 62.0);
}
