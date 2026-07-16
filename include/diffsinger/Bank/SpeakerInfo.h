#pragma once

#include <optional>
#include <string>
#include <utility>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// SpeakerInfo - Describes a speaker (voice timbre) provided by a singer.
    class DSBANK_EXPORT SpeakerInfo {
    public:
        SpeakerInfo() = default;
        SpeakerInfo(std::string speakerId, std::string name, std::string singerId);

    public:
        const std::string &speakerId() const;
        void setSpeakerId(std::string speakerId);

        const std::string &name() const;
        void setName(std::string name);

        const std::string &singerId() const;
        void setSingerId(std::string singerId);

        /// Tone range as {min, max} MIDI note numbers. nullopt when the singer
        /// config does not declare a tone range; the host should fall back to a
        /// default range and prompt the user.
        const std::optional<std::pair<int, int>> &toneRange() const;
        void setToneRange(std::optional<std::pair<int, int>> toneRange);

    protected:
        std::string _speakerId;
        std::string _name;
        std::string _singerId;
        std::optional<std::pair<int, int>> _toneRange;
    };

}
