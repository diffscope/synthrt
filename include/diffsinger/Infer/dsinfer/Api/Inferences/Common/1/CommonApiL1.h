#pragma once

#include <optional>
#include <string>
#include <vector>

#include <dsinfer/Core/ParamTag.h>

/// Some common enums and structs for different inference modules
namespace srt::svs::Api::Common::L1 {

    namespace Tags {

        /// Pitch controls
        inline constexpr ParamTag Pitch("pitch");
        inline constexpr ParamTag Expr("expr");
        inline constexpr ParamTag F0("f0");
        inline constexpr ParamTag ToneShift("tone_shift");

        /// Variance controls
        inline constexpr ParamTag Energy("energy");
        inline constexpr ParamTag Breathiness("breathiness");
        inline constexpr ParamTag Voicing("voicing");
        inline constexpr ParamTag Tension("tension");
        inline constexpr ParamTag MouthOpening("mouth_opening");

        /// Transition controls
        inline constexpr ParamTag Gender("gender");
        inline constexpr ParamTag Velocity("velocity");

    };

    enum MelBase {
        MelBase_E,
        MelBase_10,
    };

    enum MelScale {
        MelScale_Slaney,
        MelScale_HTK,
    };

    enum GlideType {
        GT_None,
        GT_Up,
        GT_Down,
    };

    enum LinguisticMode {
        LM_Word,
        LM_Phoneme,
    };

    struct InputPhonemeInfo {
        struct Speaker {
            std::string name;
            double proportion = 1;  // range: [0, 1]

            /// Optional inline speaker embedding vector.
            /// When non-empty, this vector is used directly instead of
            /// looking up \p name in the voice bank's speaker table. This
            /// allows callers to supply mixed or custom embeddings for
            /// speakers that are not predefined in the voice bank.
            /// The vector length must match the model's hiddenSize.
            std::vector<float> embedding;
        };

        std::string token;
        std::string language;
        int tone = 0;
        double start = 0;
        std::vector<Speaker> speakers;
    };

    struct InputNoteInfo {
        int key = 0;
        int cents = 0;
        double duration = 0;
        GlideType glide = GT_None;
        bool is_rest = false;
    };

    struct InputWordInfo {
        std::vector<InputPhonemeInfo> phones;
        std::vector<InputNoteInfo> notes;
    };

    struct InputParameterInfo {
        struct RetakeRange {
            double start = 0;  // seconds (include)
            double end = 0;  // seconds (exclude)
        };

        ParamTag tag;
        std::vector<double> values;
        double interval = 0;  // seconds
        std::optional<RetakeRange> retake;  // if no value, retake the full range
    };

    struct InputSpeakerInfo {
        std::string name;
        double interval = 0;  // seconds
        std::vector<double> proportions;

        /// Optional inline speaker embedding vector.
        /// When non-empty, this vector is used directly instead of
        /// looking up \p name in the voice bank's speaker table. This
        /// allows callers to supply mixed or custom embeddings for
        /// speakers that are not predefined in the voice bank.
        /// The vector length must match the model's hiddenSize.
        std::vector<float> embedding;
    };

}
