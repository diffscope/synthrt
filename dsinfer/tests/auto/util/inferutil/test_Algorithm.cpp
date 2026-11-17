#include <cmath>
#include <vector>

#include <inferutil/Algorithm.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using namespace ds::inferutil;

BOOST_AUTO_TEST_CASE(interpolation_endpoints_fill_and_midpoint_policy) {
    const std::vector<double> xs{0, 2}, ys{10, 20}, samples{-1, 0, 1, 2, 3};
    const stdc::array_view<double> x(xs), y(ys), sample(samples);
    const auto linear = interpolate(sample, x, y, -5.0, 99.0);
    BOOST_CHECK(linear == std::vector<double>({-5, 10, 15, 20, 99}));
    BOOST_CHECK(interpolate<InterpolateNearestNeighbor>(sample, x, y, -5.0, 99.0) ==
                std::vector<double>({-5, 10, 10, 20, 99}));
    BOOST_CHECK(interpolate<InterpolateNearestNeighborUp>(sample, x, y, -5.0, 99.0) ==
                std::vector<double>({-5, 10, 20, 20, 99}));
    const stdc::array_view<double> empty;
    BOOST_CHECK(interpolate(sample, empty, empty).empty());
    BOOST_CHECK(interpolate(sample, x, empty).empty());
    BOOST_CHECK(std::isnan(interpolate(sample, x, y).front()));
}

BOOST_AUTO_TEST_CASE(arange_and_resampling_boundaries) {
    BOOST_CHECK(arange(0, 5, 2) == std::vector<int>({0, 2, 4}));
    BOOST_CHECK(arange(5, 0, -2) == std::vector<int>({5, 3, 1}));
    BOOST_CHECK(arange(0, 5, 0).empty());
    BOOST_CHECK(arange(0, 5, -1).empty());
    const std::vector<double> values{0, 2, 4};
    BOOST_CHECK(resample(values, 1, 0.5, 6, false) == std::vector<double>({0, 1, 2, 3, 0, 0}));
    BOOST_CHECK(resample(values, 1, 0.5, 6, true) == std::vector<double>({0, 1, 2, 3, 3, 3}));
    BOOST_CHECK(resample(values, 1, 0.5, 2, true) == std::vector<double>({0, 1}));
    BOOST_CHECK(resample(values, 1, 0, 2, true).empty());
    BOOST_CHECK(resample(values, 1, 1, 0, true).empty());
    const std::vector<double> single{7};
    BOOST_CHECK(resample(single, 1, 1, 3, true) == std::vector<double>({7, 7, 7}));
}

BOOST_AUTO_TEST_CASE(rest_regions_use_nearest_sounding_notes) {
    std::vector<int> midi{0, 60, 0, 0, 0, 72, 0};
    BOOST_REQUIRE(fillRestMidiWithNearestInPlace(midi, {1, 0, 1, 1, 1, 0, 1}));
    BOOST_CHECK(midi == std::vector<int>({60, 60, 60, 60, 72, 72, 72}));
    const auto saved = midi;
    BOOST_CHECK(!fillRestMidiWithNearestInPlace(midi, {1}));
    BOOST_CHECK(midi == saved);
    std::vector<int> rests{0, 0};
    BOOST_CHECK(fillRestMidiWithNearestInPlace(rests, {1, 1}));
    BOOST_CHECK(rests == std::vector<int>({0, 0}));
}
