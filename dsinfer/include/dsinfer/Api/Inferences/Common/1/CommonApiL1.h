#ifndef DSINFER_API_COMMONAPIL1_H
#define DSINFER_API_COMMONAPIL1_H

#include <optional>
#include <string>
#include <vector>

#include <dsinfer/Core/ParamTag.h>

/// Some common enums and structs for different inference modules
namespace ds::Api::Common::L1 {

    namespace Tags {

        static constexpr ParamTag Pitch("pitch");

        /// Variance controls
        static constexpr ParamTag Energy("energy");
        static constexpr ParamTag Breathiness("breathiness");
        static constexpr ParamTag Voicing("voicing");
        static constexpr ParamTag Tension("tension");

        /// Transition controls
        static constexpr ParamTag Gender("gender");
        static constexpr ParamTag Velocity("velocity");

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

    struct InputPhonemeInfo {
        struct Speaker {
            std::string name;
            double proportion = 1;  // range: [0, 1]
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
    };

}

#endif // DSINFER_API_COMMONAPIL1_H