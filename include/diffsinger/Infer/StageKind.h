#pragma once

#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

namespace ds::infer {

    /// StageKind - Identifies one of the 5 DiffSinger inference pipeline stages.
    ///
    /// New stages may only be appended; existing values must not change
    /// semantics (source-compatible extension).
    enum class StageKind {
        Duration,
        Pitch,
        Variance,
        Acoustic,
        Vocoder,
    };

} // namespace ds::infer
