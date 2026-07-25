#include <diffsinger/Bank/LanguageInfo.h>
#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/SingerManifest.h>
#include <diffsinger/Bank/SpeakerInfo.h>

namespace ds::bank {

    // ---------------------------------------------------------------------------
    // LanguageInfo
    // ---------------------------------------------------------------------------

    LanguageInfo::LanguageInfo(std::string languageId, std::string name, std::string g2pVersion)
        : m_languageId(std::move(languageId)), m_name(std::move(name)),
          m_g2pVersion(std::move(g2pVersion)) {
    }

    const std::string &LanguageInfo::languageId() const {
        return m_languageId;
    }

    void LanguageInfo::setLanguageId(std::string languageId) {
        m_languageId = std::move(languageId);
    }

    const std::string &LanguageInfo::name() const {
        return m_name;
    }

    void LanguageInfo::setName(std::string name) {
        m_name = std::move(name);
    }

    const std::string &LanguageInfo::g2pVersion() const {
        return m_g2pVersion;
    }

    void LanguageInfo::setG2pVersion(std::string g2pVersion) {
        m_g2pVersion = std::move(g2pVersion);
    }

    const std::string &LanguageInfo::g2pId() const {
        return m_g2pId;
    }

    void LanguageInfo::setG2pId(std::string g2pId) {
        m_g2pId = std::move(g2pId);
    }

    const std::filesystem::path &LanguageInfo::dict() const {
        return m_dict;
    }

    void LanguageInfo::setDict(std::filesystem::path dict) {
        m_dict = std::move(dict);
    }

    const std::string &LanguageInfo::s2pMode() const {
        return m_s2pMode;
    }

    void LanguageInfo::setS2pMode(std::string s2pMode) {
        m_s2pMode = std::move(s2pMode);
    }

    const std::string &LanguageInfo::onsetMode() const {
        return m_onsetMode;
    }

    void LanguageInfo::setOnsetMode(std::string onsetMode) {
        m_onsetMode = std::move(onsetMode);
    }

    const std::filesystem::path &LanguageInfo::s2pFile() const {
        return m_s2pFile;
    }

    void LanguageInfo::setS2pFile(std::filesystem::path s2pFile) {
        m_s2pFile = std::move(s2pFile);
    }

    const std::filesystem::path &LanguageInfo::onsetFile() const {
        return m_onsetFile;
    }

    void LanguageInfo::setOnsetFile(std::filesystem::path onsetFile) {
        m_onsetFile = std::move(onsetFile);
    }

    const std::vector<std::filesystem::path> &LanguageInfo::g2pPackages() const {
        return m_g2pPackages;
    }

    void LanguageInfo::setG2pPackages(std::vector<std::filesystem::path> g2pPackages) {
        m_g2pPackages = std::move(g2pPackages);
    }

    bool LanguageInfo::hasG2pPackageVersion() const {
        return m_g2pPackageVersion.has_value();
    }

    const stdc::VersionNumber &LanguageInfo::g2pPackageVersion() const {
        return *m_g2pPackageVersion;
    }

    void LanguageInfo::setG2pPackageVersion(stdc::VersionNumber version) {
        m_g2pPackageVersion = std::move(version);
    }

    void LanguageInfo::clearG2pPackageVersion() {
        m_g2pPackageVersion.reset();
    }

    // ---------------------------------------------------------------------------
    // SpeakerInfo
    // ---------------------------------------------------------------------------

    SpeakerInfo::SpeakerInfo(std::string speakerId, std::string name, std::string singerId)
        : m_speakerId(std::move(speakerId)), m_name(std::move(name)), m_singerId(std::move(singerId)) {
    }

    const std::string &SpeakerInfo::speakerId() const {
        return m_speakerId;
    }

    void SpeakerInfo::setSpeakerId(std::string speakerId) {
        m_speakerId = std::move(speakerId);
    }

    const std::string &SpeakerInfo::name() const {
        return m_name;
    }

    void SpeakerInfo::setName(std::string name) {
        m_name = std::move(name);
    }

    const std::string &SpeakerInfo::singerId() const {
        return m_singerId;
    }

    void SpeakerInfo::setSingerId(std::string singerId) {
        m_singerId = std::move(singerId);
    }

    const std::optional<std::pair<int, int>> &SpeakerInfo::toneRange() const {
        return m_toneRange;
    }

    void SpeakerInfo::setToneRange(std::optional<std::pair<int, int>> toneRange) {
        m_toneRange = std::move(toneRange);
    }

    // ---------------------------------------------------------------------------
    // SingerManifest
    // ---------------------------------------------------------------------------

    SingerManifest::SingerManifest() : m_phonemeLength(48.0) {
    }

    SingerManifest::SingerManifest(std::string singerId, std::string name)
        : m_singerId(std::move(singerId)), m_name(std::move(name)), m_phonemeLength(48.0) {
    }

    const std::string &SingerManifest::singerId() const {
        return m_singerId;
    }

    void SingerManifest::setSingerId(std::string singerId) {
        m_singerId = std::move(singerId);
    }

    const std::string &SingerManifest::packageId() const {
        return m_packageId;
    }

    void SingerManifest::setPackageId(std::string packageId) {
        m_packageId = std::move(packageId);
    }

    stdc::VersionNumber SingerManifest::packageVersion() const {
        return m_packageVersion;
    }

    void SingerManifest::setPackageVersion(stdc::VersionNumber packageVersion) {
        m_packageVersion = std::move(packageVersion);
    }

    const std::string &SingerManifest::name() const {
        return m_name;
    }

    void SingerManifest::setName(std::string name) {
        m_name = std::move(name);
    }

    double SingerManifest::phonemeLength() const {
        return m_phonemeLength;
    }

    void SingerManifest::setPhonemeLength(double length) {
        m_phonemeLength = length;
    }

    const std::vector<LanguageInfo> &SingerManifest::languages() const {
        return m_languages;
    }

    void SingerManifest::setLanguages(std::vector<LanguageInfo> languages) {
        m_languages = std::move(languages);
    }

    const std::vector<SpeakerInfo> &SingerManifest::speakers() const {
        return m_speakers;
    }

    void SingerManifest::setSpeakers(std::vector<SpeakerInfo> speakers) {
        m_speakers = std::move(speakers);
    }

    const std::string &SingerManifest::defaultLanguage() const {
        return m_defaultLanguage;
    }

    void SingerManifest::setDefaultLanguage(std::string defaultLanguage) {
        m_defaultLanguage = std::move(defaultLanguage);
    }

    const std::vector<SingerImportInfo> &SingerManifest::imports() const {
        return m_imports;
    }

    void SingerManifest::setImports(std::vector<SingerImportInfo> imports) {
        m_imports = std::move(imports);
    }

    // ---------------------------------------------------------------------------
    // PackageManifest
    // ---------------------------------------------------------------------------

    const std::string &PackageManifest::packageId() const {
        return m_packageId;
    }

    void PackageManifest::setPackageId(std::string packageId) {
        m_packageId = std::move(packageId);
    }

    const std::filesystem::path &PackageManifest::rootPath() const {
        return m_rootPath;
    }

    void PackageManifest::setRootPath(std::filesystem::path rootPath) {
        m_rootPath = std::move(rootPath);
    }

    const std::vector<std::filesystem::path> &PackageManifest::singerRefs() const {
        return m_singerRefs;
    }

    void PackageManifest::setSingerRefs(std::vector<std::filesystem::path> singerRefs) {
        m_singerRefs = std::move(singerRefs);
    }

    const std::vector<std::filesystem::path> &PackageManifest::inferenceRefs() const {
        return m_inferenceRefs;
    }

    void PackageManifest::setInferenceRefs(std::vector<std::filesystem::path> inferenceRefs) {
        m_inferenceRefs = std::move(inferenceRefs);
    }

    stdc::VersionNumber PackageManifest::version() const {
        return m_version;
    }

    void PackageManifest::setVersion(stdc::VersionNumber version) {
        m_version = std::move(version);
    }

    const std::optional<stdc::VersionNumber> &PackageManifest::compatVersion() const {
        return m_compatVersion;
    }

    void PackageManifest::setCompatVersion(std::optional<stdc::VersionNumber> compatVersion) {
        m_compatVersion = std::move(compatVersion);
    }

    const std::string &PackageManifest::name() const {
        return m_name;
    }

    void PackageManifest::setName(std::string name) {
        m_name = std::move(name);
    }

    const std::string &PackageManifest::description() const {
        return m_description;
    }

    void PackageManifest::setDescription(std::string description) {
        m_description = std::move(description);
    }

    const std::string &PackageManifest::author() const {
        return m_author;
    }

    void PackageManifest::setAuthor(std::string author) {
        m_author = std::move(author);
    }

    const std::string &PackageManifest::license() const {
        return m_license;
    }

    void PackageManifest::setLicense(std::string license) {
        m_license = std::move(license);
    }

    const std::vector<std::string> &PackageManifest::dependencies() const {
        return m_dependencies;
    }

    void PackageManifest::setDependencies(std::vector<std::string> dependencies) {
        m_dependencies = std::move(dependencies);
    }

    const std::vector<SingerManifest> &PackageManifest::singers() const {
        return m_singers;
    }

    void PackageManifest::setSingers(std::vector<SingerManifest> singers) {
        m_singers = std::move(singers);
    }

    const std::vector<SpeakerInfo> &PackageManifest::speakers() const {
        return m_speakers;
    }

    void PackageManifest::setSpeakers(std::vector<SpeakerInfo> speakers) {
        m_speakers = std::move(speakers);
    }

    const std::vector<LanguageInfo> &PackageManifest::languages() const {
        return m_languages;
    }

    void PackageManifest::setLanguages(std::vector<LanguageInfo> languages) {
        m_languages = std::move(languages);
    }

    const std::vector<InferenceInfo> &PackageManifest::inferences() const {
        return m_inferences;
    }

    void PackageManifest::setInferences(std::vector<InferenceInfo> inferences) {
        m_inferences = std::move(inferences);
    }

    const std::vector<srt::core::Diagnostic> &PackageManifest::diagnostics() const {
        return m_diagnostics;
    }

    void PackageManifest::addDiagnostic(srt::core::Diagnostic diagnostic) {
        m_diagnostics.emplace_back(std::move(diagnostic));
    }

}
