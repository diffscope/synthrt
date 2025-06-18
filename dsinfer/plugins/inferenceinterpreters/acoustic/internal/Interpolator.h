#ifndef DSINFER_INTERPOLATOR_H
#define DSINFER_INTERPOLATOR_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace ds {
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
    inline std::vector<T> interpolate(const std::vector<T> &samplePoints,
                                      const std::vector<T> &referencePoints,
                                      const std::vector<T> &referenceValues,
                                      T leftFillValue = std::numeric_limits<T>::quiet_NaN(),
                                      T rightFillValue = std::numeric_limits<T>::quiet_NaN()) {

        static_assert(std::is_floating_point_v<T>, "T must be a floating point type");

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

}
#endif // DSINFER_INTERPOLATOR_H
