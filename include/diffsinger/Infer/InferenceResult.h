#pragma once

#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

namespace ds::infer {

    /// InferenceResult - Output of the inference pipeline.
    ///
    /// \see 02-module-contracts.md section 4.2
    struct DSINFER_EXPORT InferenceResult {
        std::vector<float> audio;        ///< PCM samples
        int sampleRate = 0;
        int channels = 0;
        srt::core::Diagnostic error;
    };

} // namespace ds::infer
