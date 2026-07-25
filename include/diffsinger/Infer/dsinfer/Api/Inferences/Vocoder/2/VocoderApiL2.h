#pragma once

#include <vector>

#include <synthrt/Core/Task/ITask.h>
#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

namespace srt::svs::Api::Vocoder::L2 {

    /// L2 API name. Distinct from L1 ("vocoder") so consumers can dispatch
    /// between L1 and L2 results by \c objectName().
    inline constexpr char API_NAME[] = "vocoder.L2";

    inline constexpr char API_CLASS[] = "ai.svs.VocoderInference";

    inline constexpr int API_LEVEL = 2;

    /// VocoderResult (Level 2) - breaking change vs L1.
    ///
    /// Changes vs L1 (per ARCH-02, breaking changes bump the Level):
    ///  - \c audioData: \c vector<uint8_t> -> \c vector<float>
    ///    (BUG-16: the L1 \c uint8_t storage forced callers to
    ///    \c reinterpret_cast assuming float32 PCM; L2 stores float32 PCM
    ///    directly).
    ///  - New \c sampleRate / \c channels fields (BUG-05: L1 had none, so
    ///    callers hardcoded 44100 / mono when writing WAV).
    ///
    /// L1 VocoderConfiguration / VocoderStartInput / VocoderInitArgs remain
    /// reusable; only the result struct needed a breaking bump.
    class DSINFER_EXPORT VocoderResult : public srt::core::TaskResult {
    public:
        inline VocoderResult() : srt::core::TaskResult(API_NAME) {
        }

        /// float32 PCM samples, interleaved when \c channels > 1.
        std::vector<float> audioData;

        /// Sample rate in Hz.
        int sampleRate = 0;

        /// Number of audio channels.
        int channels = 0;
    };

}
