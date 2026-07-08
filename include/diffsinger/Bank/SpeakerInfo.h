#pragma once

#include <string>

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

    protected:
        std::string _speakerId;
        std::string _name;
        std::string _singerId;
    };

}
