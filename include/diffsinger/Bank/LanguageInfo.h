#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// LanguageInfo - Describes a language supported by a DiffSinger package.
    class DSBANK_EXPORT LanguageInfo {
    public:
        LanguageInfo() = default;
        LanguageInfo(std::string languageId, std::string name, std::string g2pVersion);

    public:
        const std::string &languageId() const;
        void setLanguageId(std::string languageId);

        const std::string &name() const;
        void setName(std::string name);

        const std::string &g2pVersion() const;
        void setG2pVersion(std::string g2pVersion);

        const std::string &g2pId() const;
        void setG2pId(std::string g2pId);

        const std::filesystem::path &dict() const;
        void setDict(std::filesystem::path dict);

        const std::string &s2pMode() const;
        void setS2pMode(std::string s2pMode);

        const std::string &onsetMode() const;
        void setOnsetMode(std::string onsetMode);

        const std::filesystem::path &s2pFile() const;
        void setS2pFile(std::filesystem::path s2pFile);

        const std::filesystem::path &onsetFile() const;
        void setOnsetFile(std::filesystem::path onsetFile);

        const std::vector<std::filesystem::path> &g2pPackages() const;
        void setG2pPackages(std::vector<std::filesystem::path> g2pPackages);

        bool hasG2pPackageVersion() const;
        const stdc::VersionNumber &g2pPackageVersion() const;
        void setG2pPackageVersion(stdc::VersionNumber version);
        void clearG2pPackageVersion();

    protected:
        std::string m_languageId;
        std::string m_name;
        std::string m_g2pVersion;
        std::string m_g2pId;
        std::filesystem::path m_dict;
        std::string m_s2pMode;
        std::string m_onsetMode;
        std::filesystem::path m_s2pFile;
        std::filesystem::path m_onsetFile;
        std::vector<std::filesystem::path> m_g2pPackages;
        std::optional<stdc::VersionNumber> m_g2pPackageVersion;
    };

}
