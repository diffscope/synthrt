#ifndef DSINFER_API_COMMONAPIL1_H
#define DSINFER_API_COMMONAPIL1_H

#include <optional>
#include <string>
#include <vector>

#include <dsinfer/Core/ParamTag.h>

/// Common Level 1 types shared by the DiffSinger inference interfaces.
namespace ds::Api::Common::L1 {

    namespace Tags {

        /// Predicted or supplied pitch in semitones.
        inline const ParamTag Pitch("pitch");

        /// Pitch expressiveness control.
        inline const ParamTag Expr("expr");

        /// Fundamental frequency in hertz.
        inline const ParamTag F0("f0");

        /// Relative pitch transposition in semitones.
        inline const ParamTag ToneShift("tone_shift");

        /// Energy control.
        inline const ParamTag Energy("energy");

        /// Breathiness control.
        inline const ParamTag Breathiness("breathiness");

        /// Voicing control.
        inline const ParamTag Voicing("voicing");

        /// Vocal tension control.
        inline const ParamTag Tension("tension");

        /// Mouth opening control.
        inline const ParamTag MouthOpening("mouth_opening");

        /// Perceived gender shift control.
        inline const ParamTag Gender("gender");

        /// Articulation velocity control.
        inline const ParamTag Velocity("velocity");

    };

    /// Selects the logarithmic base used by the mel transform.
    enum class MelBase {
        E,      ///< Uses the natural logarithm.
        Base10, ///< Uses the base 10 logarithm.
    };

    /// Selects the frequency scale used by the mel transform.
    enum class MelScale {
        Slaney, ///< Uses the Slaney mel scale.
        HTK,    ///< Uses the HTK mel scale.
    };

    /// Describes the pitch glide applied to a note transition.
    enum class GlideType {
        None, ///< Applies no pitch glide.
        Up,   ///< Glides upward into the note.
        Down, ///< Glides downward into the note.
    };

    /// Selects the granularity accepted by a linguistic encoder.
    enum class LinguisticMode {
        Word,    ///< Encodes word level timing information.
        Phoneme, ///< Encodes phoneme level timing information.
    };

    /// Describes one phoneme in the linguistic input sequence.
    struct InputPhonemeInfo {
        /// Assigns a speaker contribution to this phoneme.
        struct Speaker {
            /// Speaker identifier exposed by the imported inference module.
            std::string name;

            /// Relative speaker contribution in the inclusive range from 0 to 1.
            double proportion = 1;
        };

        /// Phoneme token understood by the imported inference module.
        std::string token;

        /// Language identifier associated with the phoneme.
        std::string language;

        /// Language specific lexical tone value.
        int tone = 0;

        /// Start time of the phoneme in seconds.
        double start = 0;

        /// Speaker mixture applied to the phoneme.
        std::vector<Speaker> speakers;
    };

    /// Describes one musical note in the linguistic input sequence.
    struct InputNoteInfo {
        /// MIDI note number.
        int key = 0;

        /// Pitch offset from \c key in cents.
        int cents = 0;

        /// Note duration in seconds.
        double duration = 0;

        /// Pitch glide applied at the note boundary.
        GlideType glide = GlideType::None;

        /// Indicates that the note represents a rest.
        bool is_rest = false;
    };

    /// Groups the phonemes and notes belonging to one word.
    struct InputWordInfo {
        /// Phonemes in pronunciation order.
        std::vector<InputPhonemeInfo> phones;

        /// Notes aligned with this word.
        std::vector<InputNoteInfo> notes;
    };

    /// Supplies one sampled control curve to an inference task.
    struct InputParameterInfo {
        /// Selects a half open time range that should be recomputed.
        struct RetakeRange {
            /// Inclusive start time in seconds.
            double start = 0;

            /// Exclusive end time in seconds.
            double end = 0;
        };

        /// Identifies the parameter represented by \c values.
        ParamTag tag;

        /// Samples of the parameter curve.
        std::vector<double> values;

        /// Time between adjacent samples in seconds.
        double interval = 0;

        /// Range to recompute, or no value to recompute the complete curve.
        std::optional<RetakeRange> retake;
    };

    /// Supplies a sampled speaker mixture to an inference task.
    struct InputSpeakerInfo {
        /// Speaker identifier exposed by the imported inference module.
        std::string name;

        /// Time between adjacent proportion samples in seconds.
        double interval = 0;

        /// Speaker proportions sampled at \c interval.
        std::vector<double> proportions;
    };

}

#endif // DSINFER_API_COMMONAPIL1_H
