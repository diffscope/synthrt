#pragma once

#include <string>
#include <map>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/dsbank_global.h>
#include <diffsinger/Bank/LanguageInfo.h>
#include <diffsinger/Bank/SpeakerInfo.h>

namespace ds::bank {

    struct DSBANK_EXPORT SingerImportInfo {
        std::string inferenceId;
        std::map<std::string, std::string> speakerMapping;
    };

    /// SingerManifest - Describes a singer (voicebank) declared by a DiffSinger
    /// package. Pure package metadata; runtime resolution state lives in the
    /// session SingerSnapshot.
    ///
    /// \c phonemeLength defaults to 48 (the DiffSinger convention).
    class DSBANK_EXPORT SingerManifest {
    public:
        SingerManifest();
        SingerManifest(std::string singerId, std::string name);

    public:
        const std::string &singerId() const;
        void setSingerId(std::string singerId);

        const std::string &packageId() const;
        void setPackageId(std::string packageId);

        stdc::VersionNumber packageVersion() const;
        void setPackageVersion(stdc::VersionNumber packageVersion);

        const std::string &name() const;
        void setName(std::string name);

        double phonemeLength() const;
        void setPhonemeLength(double length);

        const std::vector<LanguageInfo> &languages() const;
        void setLanguages(std::vector<LanguageInfo> languages);

        const std::vector<SpeakerInfo> &speakers() const;
        void setSpeakers(std::vector<SpeakerInfo> speakers);

        const std::string &defaultLanguage() const;
        void setDefaultLanguage(std::string defaultLanguage);

        const std::vector<SingerImportInfo> &imports() const;
        void setImports(std::vector<SingerImportInfo> imports);

    protected:
        std::string m_singerId;
        std::string m_packageId;
        stdc::VersionNumber m_packageVersion;
        std::string m_name;
        double m_phonemeLength;
        std::vector<LanguageInfo> m_languages;
        std::vector<SpeakerInfo> m_speakers;
        std::string m_defaultLanguage;
        std::vector<SingerImportInfo> m_imports;
    };

}
