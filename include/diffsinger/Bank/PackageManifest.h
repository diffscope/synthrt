#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/dsbank_global.h>
#include <diffsinger/Bank/LanguageInfo.h>
#include <diffsinger/Bank/SingerManifest.h>
#include <diffsinger/Bank/SpeakerInfo.h>

namespace ds::bank {

    struct DSBANK_EXPORT InferenceInfo {
        std::string id;
        std::string className;
        std::filesystem::path configPath;
        int level = 0;
        std::vector<std::string> resourcePaths;
        std::map<std::string, std::filesystem::path> modelPaths;
        std::filesystem::path phonemesPath;
        std::filesystem::path languagesPath;
        std::map<std::string, std::filesystem::path> speakerEmbeddings;
        std::vector<std::string> parameters;
        int hiddenSize = 0;
        int sampleRate = 0;
        int hopSize = 0;
        double frameWidth = 0.0;
        bool useLanguageId = false;
        bool useSpeakerEmbedding = false;
        bool useContinuousAcceleration = false;
    };

    /// PackageManifest - In-memory representation of a DiffSinger package manifest.
    /// Standard packages use \c desc.json. Missing standard manifests are
    /// reported as parse errors; legacy \c package.json is not a voicebank path.
    class DSBANK_EXPORT PackageManifest {
    public:
        PackageManifest() = default;

    public:
        const std::string &packageId() const;
        void setPackageId(std::string packageId);

        const std::filesystem::path &rootPath() const;
        void setRootPath(std::filesystem::path rootPath);

        const std::vector<std::filesystem::path> &singerRefs() const;
        void setSingerRefs(std::vector<std::filesystem::path> singerRefs);

        const std::vector<std::filesystem::path> &inferenceRefs() const;
        void setInferenceRefs(std::vector<std::filesystem::path> inferenceRefs);

        stdc::VersionNumber version() const;
        void setVersion(stdc::VersionNumber version);

        const std::optional<stdc::VersionNumber> &compatVersion() const;
        void setCompatVersion(std::optional<stdc::VersionNumber> compatVersion);

        const std::string &name() const;
        void setName(std::string name);

        const std::string &description() const;
        void setDescription(std::string description);

        const std::string &author() const;
        void setAuthor(std::string author);

        const std::string &license() const;
        void setLicense(std::string license);

        const std::vector<std::string> &dependencies() const;
        void setDependencies(std::vector<std::string> dependencies);

        const std::vector<SingerManifest> &singers() const;
        void setSingers(std::vector<SingerManifest> singers);

        const std::vector<SpeakerInfo> &speakers() const;
        void setSpeakers(std::vector<SpeakerInfo> speakers);

        const std::vector<LanguageInfo> &languages() const;
        void setLanguages(std::vector<LanguageInfo> languages);

        const std::vector<InferenceInfo> &inferences() const;
        void setInferences(std::vector<InferenceInfo> inferences);

    protected:
        std::string _packageId;
        std::filesystem::path _rootPath;
        std::vector<std::filesystem::path> _singerRefs;
        std::vector<std::filesystem::path> _inferenceRefs;
        stdc::VersionNumber _version;
        std::optional<stdc::VersionNumber> _compatVersion;
        std::string _name;
        std::string _description;
        std::string _author;
        std::string _license;
        std::vector<std::string> _dependencies;
        std::vector<SingerManifest> _singers;
        std::vector<SpeakerInfo> _speakers;
        std::vector<LanguageInfo> _languages;
        std::vector<InferenceInfo> _inferences;
    };

}
