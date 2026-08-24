#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/DisplayText.h>

#include <diffsinger/Bank/dsbank_global.h>
#include <diffsinger/Bank/LanguageInfo.h>
#include <diffsinger/Bank/SingerManifest.h>
#include <diffsinger/Bank/SpeakerInfo.h>

namespace ds::bank {

    struct DSBANK_EXPORT InferenceInfo {
        std::string id;
        std::string className;
        /// Package identity this inference belongs to. Set by PackageParser
        /// from the owning PackageManifest's packageId. Used by ModelRegistry /
        /// SpeakerMapper to isolate inferences that share the same id across
        /// different packages (ARCH-06 cross-package stage sharing).
        std::string packageId;
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

        /// 以下人读字段为多语言文本（ds-spec 2.4 §多语言文本），全部翻译随
        /// 对象保留；键对 Runtime 不透明（区分大小写），宿主按自有匹配
        /// 策略以 locales()/text(key) 精确直取，text() 为默认（"_"）文本。
        /// 切换 UI 语言无需重新解析 desc.json。
        const srt::core::DisplayText &name() const;
        void setName(srt::core::DisplayText name);

        const srt::core::DisplayText &description() const;
        void setDescription(srt::core::DisplayText description);

        /// 来自 desc.json 的 "vendor" 字段（ds-spec 2.4）。
        const srt::core::DisplayText &author() const;
        void setAuthor(srt::core::DisplayText author);

        /// 来自 desc.json 的 "copyright" 字段（ds-spec 2.4 标准字段名；
        /// 历史写法 "license" 不再读取）。
        const srt::core::DisplayText &license() const;
        void setLicense(srt::core::DisplayText license);

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

        /// Non-fatal issues encountered while parsing in relaxed mode.
        const std::vector<srt::core::Diagnostic> &diagnostics() const;
        void addDiagnostic(srt::core::Diagnostic diagnostic);

    protected:
        std::string m_packageId;
        std::filesystem::path m_rootPath;
        std::vector<std::filesystem::path> m_singerRefs;
        std::vector<std::filesystem::path> m_inferenceRefs;
        stdc::VersionNumber m_version;
        std::optional<stdc::VersionNumber> m_compatVersion;
        srt::core::DisplayText m_name;
        srt::core::DisplayText m_description;
        srt::core::DisplayText m_author;
        srt::core::DisplayText m_license;
        std::vector<std::string> m_dependencies;
        std::vector<SingerManifest> m_singers;
        std::vector<SpeakerInfo> m_speakers;
        std::vector<LanguageInfo> m_languages;
        std::vector<InferenceInfo> m_inferences;
        std::vector<srt::core::Diagnostic> m_diagnostics;
    };

}
