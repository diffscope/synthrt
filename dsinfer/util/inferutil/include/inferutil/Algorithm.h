#ifndef DSINFER_INFERUTIL_ALGORITHM_H
#define DSINFER_INFERUTIL_ALGORITHM_H

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>
#include <stdcorelib/adt/array_view.h>

namespace ds::inferutil {
    enum InterpolationMethod {
        InterpolateLinear,
        InterpolateCubicSpline,
        InterpolateNearestNeighbor,
        InterpolateNearestNeighborUp
    };

    template <typename T>
    inline T interpolatePointLinear(T x0, T y0, T x1, T y1, T x) {
        return y0 + (x - x0) * (y1 - y0) / (x1 - x0);
    }

    template <class T>
    inline T interpolateNearestNeighbor(T x0, T y0, T x1, T y1, T x) {
        return ((x - x0) > (x1 - x)) ? y1 : y0;
    }

    template <class T>
    inline T interpolateNearestNeighborUp(T x0, T y0, T x1, T y1, T x) {
        return ((x - x0) >= (x1 - x)) ? y1 : y0;
    }

    template <InterpolationMethod Method = InterpolateLinear, typename T = double>
    inline std::vector<T> interpolate(const stdc::array_view<T> &samplePoints,
                                      const stdc::array_view<T> &referencePoints,
                                      const stdc::array_view<T> &referenceValues,
                                      T leftFillValue = std::numeric_limits<T>::quiet_NaN(),
                                      T rightFillValue = std::numeric_limits<T>::quiet_NaN()) {

        static_assert(std::is_floating_point_v<T>, "T must be a floating point type");

        // Without reference points there is nothing to interpolate from, and \c front() and
        // \c back() below would read out of bounds. \a referenceValues is indexed with
        // \a referencePoints' indices, so it must be at least as long.
        if (referencePoints.empty() || referenceValues.size() < referencePoints.size()) {
            return {};
        }

        std::vector<T> interpolatedValues;
        interpolatedValues.reserve(samplePoints.size());

        for (const auto &samplePoint : samplePoints) {
            if (samplePoint < referencePoints.front()) {
                interpolatedValues.push_back(leftFillValue);
            } else if (samplePoint > referencePoints.back()) {
                interpolatedValues.push_back(rightFillValue);
            } else {
                auto it =
                    std::lower_bound(referencePoints.begin(), referencePoints.end(), samplePoint);
                size_t index = std::distance(referencePoints.begin(), it);

                if (it != referencePoints.end() && *it == samplePoint) {
                    interpolatedValues.push_back(referenceValues[index]);
                } else {
                    auto x0 = referencePoints[index - 1];
                    auto x1 = referencePoints[index];
                    auto y0 = referenceValues[index - 1];
                    auto y1 = referenceValues[index];
                    T interpolatedValue;

                    if constexpr (Method == InterpolateLinear) {
                        interpolatedValue = interpolatePointLinear(x0, y0, x1, y1, samplePoint);
                    } else if constexpr (Method == InterpolateNearestNeighbor) {
                        interpolatedValue = interpolateNearestNeighbor(x0, y0, x1, y1, samplePoint);
                    } else if constexpr (Method == InterpolateNearestNeighborUp) {
                        interpolatedValue =
                            interpolateNearestNeighborUp(x0, y0, x1, y1, samplePoint);
                    } else {
                        interpolatedValue = interpolatePointLinear(x0, y0, x1, y1, samplePoint);
                    }

                    interpolatedValues.push_back(interpolatedValue);
                }
            }
        }

        return interpolatedValues;
    }

    template <class T>
    inline std::vector<T> arange(T start, T stop, T step) {
        // A zero step never reaches \a stop, and a step pointing away from it yields nothing.
        // Both have to be caught here: \c std::ceil() would produce infinity or NaN, which is
        // undefined behaviour once cast to \c size_t.
        if (step == T(0)) {
            return {};
        }
        if (step > T(0) ? (stop <= start) : (stop >= start)) {
            return {};
        }

        const double count = std::ceil((static_cast<double>(stop) - static_cast<double>(start)) /
                                       static_cast<double>(step));
        if (!std::isfinite(count) || count < 1.0) {
            return {};
        }
        const auto size = static_cast<size_t>(count);

        std::vector<T> result;
        result.reserve(size);

        for (size_t i = 0; i < size; ++i) {
            result.push_back(start + static_cast<T>(i) * step);
        }

        return result;
    }

    inline std::vector<double> resample(const stdc::array_view<double> &samples, double timestep,
                                               double targetTimestep, int64_t targetLength,
                                               bool fillLast) {
        if (samples.empty() || targetLength == 0) {
            return {};
        }
        if (samples.size() == 1) {
            std::vector<double> result(targetLength, samples[0]);
            return result;
        }
        if (timestep == 0 || targetTimestep == 0) {
            return {};
        }
        if (targetLength == 1) {
            return {samples[0]};
        }
        // Find the time duration of input samples in seconds.
        auto tMax = static_cast<double>(samples.size() - 1) * timestep;

        // Construct target time axis for interpolation.
        auto targetTimeAxis = arange(0.0, tMax, targetTimestep);

        // Construct input time axis (for interpolation).
        auto inputTimeAxis = arange(0.0, static_cast<double>(samples.size()), 1.0);
        std::transform(inputTimeAxis.begin(), inputTimeAxis.end(), inputTimeAxis.begin(),
                       [timestep](double value) { return value * timestep; });

        // Interpolate sample curve to target time axis
        auto targetSamples =
            interpolate<InterpolateLinear, double>(targetTimeAxis, inputTimeAxis, samples);

        // \c interpolate() returns empty when it rejects its input, which would make \c back()
        // below read out of bounds. Callers already treat an empty result as a failed resample.
        if (targetSamples.empty()) {
            return {};
        }

        // Resize the interpolated curve vector to target length
        auto actualLength = static_cast<int64_t>(targetSamples.size());

        if (actualLength > targetLength) {
            // Truncate vector to target length
            targetSamples.resize(targetLength);
        } else if (actualLength < targetLength) {
            // Expand vector to target length, filling last value
            double tailFillValue = fillLast ? targetSamples.back() : 0;
            targetSamples.resize(targetLength, tailFillValue);
        }
        return targetSamples;
    }

    template <typename T>
    inline bool fillRestMidiWithNearestInPlace(std::vector<T> &midi,
                                               const std::vector<uint8_t> &isRest) {

        static_assert(std::is_floating_point_v<T> ||
            (std::is_integral_v<T> && !std::is_same_v<T, bool>));

        if (midi.size() != isRest.size()) {
            return false;
        }

        const size_t n = midi.size();

        size_t start = 0;
        while (start < n) {
            // Skip non-rest elements
            while (start < n && !isRest[start]) {
                ++start;
            }

            if (start >= n)
                break;

            size_t end = start;
            // Find contiguous rest region
            while (end < n && isRest[end]) {
                ++end;
            }

            // Handle [start, end)
            if (start > 0 && end < n) {
                // Middle segment
                auto left_val = midi[start - 1];
                auto right_val = midi[end];

                size_t mid = start + (end - start + 1) / 2; // split evenly

                for (size_t i = start; i < mid; ++i) {
                    midi[i] = left_val;
                }
                for (size_t i = mid; i < end; ++i) {
                    midi[i] = right_val;
                }
            } else if (start > 0) {
                // End segment
                auto fill_val = midi[start - 1];
                for (size_t i = start; i < end; ++i) {
                    midi[i] = fill_val;
                }
            } else if (end < n) {
                // Start segment
                auto fill_val = midi[end];
                for (size_t i = start; i < end; ++i) {
                    midi[i] = fill_val;
                }
            }

            start = end;
        }

        return true;
    }

}

#endif // DSINFER_INFERUTIL_ALGORITHM_H