#pragma once

#include <optional>
#include <string>
#include <utility>

#include <synthrt/Core/Support/DisplayText.h>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// SpeakerInfo - Describes a speaker (voice timbre) provided by a singer.
    class DSBANK_EXPORT SpeakerInfo {
    public:
        SpeakerInfo() = default;
        SpeakerInfo(std::string speakerId, srt::core::DisplayText name, std::string singerId);

    public:
        const std::string &speakerId() const;
        void setSpeakerId(std::string speakerId);

        /// Display name, all translations retained (ds-spec 2.4 多语言文本).
        /// Resolve with text(locale) using a BCP 47 preference tag.
        const srt::core::DisplayText &name() const;
        void setName(srt::core::DisplayText name);

        const std::string &singerId() const;
        void setSingerId(std::string singerId);

        /// Tone range as {min, max} MIDI note numbers. nullopt when the singer
        /// config does not declare a tone range; the host should fall back to a
        /// default range and prompt the user.
        const std::optional<std::pair<int, int>> &toneRange() const;
        void setToneRange(std::optional<std::pair<int, int>> toneRange);

    protected:
        std::string m_speakerId;
        srt::core::DisplayText m_name;
        std::string m_singerId;
        std::optional<std::pair<int, int>> m_toneRange;
    };

}
