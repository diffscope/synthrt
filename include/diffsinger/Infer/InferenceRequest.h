#pragma once

#include <string>
#include <vector>

#include "dsinfer/Api/Inferences/Common/1/CommonApiL1.h"

#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

namespace ds::infer {

    /// InferenceRequest - SVS-level input to the 5-stage inference pipeline.
    ///
    /// Carries pre-assembled words (phonemes + notes + speakers), curve
    /// parameters, and acoustic scalar fields. The caller (host/CLI) is
    /// responsible for building words from G2P + S2P output; InferenceService
    /// runs the full duration -> pitch -> variance -> acoustic -> vocoder
    /// pipeline on this request.
    ///
    /// \see 02-module-contracts.md section 4.1
    struct DSINFER_EXPORT InferenceRequest {
        using WordInfo = srt::svs::Api::Common::L1::InputWordInfo;
        using ParameterInfo = srt::svs::Api::Common::L1::InputParameterInfo;
        using SpeakerInfo = srt::svs::Api::Common::L1::InputSpeakerInfo;

        std::string singerId; ///< Target singer id
        std::string inferenceId;
        std::string speakerId;                 ///< Default speaker name
        std::vector<WordInfo> words;           ///< Pre-assembled words (phones+notes)
        std::vector<ParameterInfo> parameters; ///< Curve parameters (pitch/expr/...)
        std::vector<SpeakerInfo> speakers;     ///< Speaker mix curves

        double duration = 0; ///< Total audio duration (seconds)
        int64_t steps = 10;  ///< Acoustic diffusion steps
        float depth = 0.0f;  ///< Acoustic diffusion depth
    };

} // namespace ds::infer
