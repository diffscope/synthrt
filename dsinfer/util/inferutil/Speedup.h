#ifndef DSINFER_INFERUTIL_SPEEDUP_H
#define DSINFER_INFERUTIL_SPEEDUP_H

#include <cstdint>

namespace ds::inferutil {
    inline constexpr int64_t getSpeedupFromSteps(int64_t steps, int64_t fallback = 10) {
        int64_t speedup = fallback;
        if (steps > 0) {
            speedup = 1000 / steps;
            if (speedup < 1) {
                speedup = 1;
            } else if (speedup > 1000) {
                speedup = 1000;
            }
            while (((1000 % speedup) != 0) && (speedup > 1)) {
                --speedup;
            }
        }
        return speedup;
    }
}
#endif // DSINFER_INFERUTIL_SPEEDUP_H