#include <diffsinger/Bank/LanguageInfo.h>
#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/SingerManifest.h>
#include <diffsinger/Bank/SpeakerInfo.h>

namespace ds::bank {

    // ---------------------------------------------------------------------------
    // LanguageInfo
    // ---------------------------------------------------------------------------

    LanguageInfo::LanguageInfo(std::string languageId, std::string name, std::string g2pVersion)
        : _languageId(std::move(languageId)), _name(std::move(name)),
          _g2pVersion(std::move(g2pVersion)) {
    }

    const std::string &LanguageInfo::languageId() const {
        return _languageId;
    }

    void LanguageInfo::setLanguageId(std::string languageId) {
        _languageId = std::move(languageId);
    }

    const std::string &LanguageInfo::name() const {
        return _name;
    }

    void LanguageInfo::setName(std::string name) {
        _name = std::move(name);
    }

    const std::string &LanguageInfo::g2pVersion() const {
        return _g2pVersion;
    }

    void LanguageInfo::setG2pVersion(std::string g2pVersion) {
        _g2pVersion = std::move(g2pVersion);
    }

    const std::string &LanguageInfo::g2pId() const {
        return _g2pId;
    }

    void LanguageInfo::setG2pId(std::string g2pId) {
        _g2pId = std::move(g2pId);
    }

    const std::filesystem::path &LanguageInfo::dict() const {
        return _dict;
    }

    void LanguageInfo::setDict(std::filesystem::path dict) {
        _dict = std::move(dict);
    }

    const std::string &LanguageInfo::s2pMode() const {
        return _s2pMode;
    }

    void LanguageInfo::setS2pMode(std::string s2pMode) {
        _s2pMode = std::move(s2pMode);
    }

    const std::string &LanguageInfo::onsetMode() const {
        return _onsetMode;
    }

    void LanguageInfo::setOnsetMode(std::string onsetMode) {
        _onsetMode = std::move(onsetMode);
    }

    const std::filesystem::path &LanguageInfo::s2pFile() const {
        return _s2pFile;
    }

    void LanguageInfo::setS2pFile(std::filesystem::path s2pFile) {
        _s2pFile = std::move(s2pFile);
    }

    const std::filesystem::path &LanguageInfo::onsetFile() const {
        return _onsetFile;
    }

    void LanguageInfo::setOnsetFile(std::filesystem::path onsetFile) {
        _onsetFile = std::move(onsetFile);
    }

    const std::vector<std::filesystem::path> &LanguageInfo::g2pPackages() const {
        return _g2pPackages;
    }

    void LanguageInfo::setG2pPackages(std::vector<std::filesystem::path> g2pPackages) {
        _g2pPackages = std::move(g2pPackages);
    }

    bool LanguageInfo::hasG2pPackageVersion() const {
        return _g2pPackageVersion.has_value();
    }

    const stdc::VersionNumber &LanguageInfo::g2pPackageVersion() const {
        return *_g2pPackageVersion;
    }

    void LanguageInfo::setG2pPackageVersion(stdc::VersionNumber version) {
        _g2pPackageVersion = std::move(version);
    }

    void LanguageInfo::clearG2pPackageVersion() {
        _g2pPackageVersion.reset();
    }

    // ---------------------------------------------------------------------------
    // SpeakerInfo
    // ---------------------------------------------------------------------------

    SpeakerInfo::SpeakerInfo(std::string speakerId, std::string name, std::string singerId)
        : _speakerId(std::move(speakerId)), _name(std::move(name)), _singerId(std::move(singerId)) {
    }

    const std::string &SpeakerInfo::speakerId() const {
        return _speakerId;
    }

    void SpeakerInfo::setSpeakerId(std::string speakerId) {
        _speakerId = std::move(speakerId);
    }

    const std::string &SpeakerInfo::name() const {
        return _name;
    }

    void SpeakerInfo::setName(std::string name) {
        _name = std::move(name);
    }

    const std::string &SpeakerInfo::singerId() const {
        return _singerId;
    }

    void SpeakerInfo::setSingerId(std::string singerId) {
        _singerId = std::move(singerId);
    }

    const std::optional<std::pair<int, int>> &SpeakerInfo::toneRange() const {
        return _toneRange;
    }

    void SpeakerInfo::setToneRange(std::optional<std::pair<int, int>> toneRange) {
        _toneRange = std::move(toneRange);
    }

    // ---------------------------------------------------------------------------
    // SingerManifest
    // ---------------------------------------------------------------------------

    SingerManifest::SingerManifest() : _phonemeLength(48.0) {
    }

    SingerManifest::SingerManifest(std::string singerId, std::string name)
        : _singerId(std::move(singerId)), _name(std::move(name)), _phonemeLength(48.0) {
    }

    const std::string &SingerManifest::singerId() const {
        return _singerId;
    }

    void SingerManifest::setSingerId(std::string singerId) {
        _singerId = std::move(singerId);
    }

    const std::string &SingerManifest::packageId() const {
        return _packageId;
    }

    void SingerManifest::setPackageId(std::string packageId) {
        _packageId = std::move(packageId);
    }

    stdc::VersionNumber SingerManifest::packageVersion() const {
        return _packageVersion;
    }

    void SingerManifest::setPackageVersion(stdc::VersionNumber packageVersion) {
        _packageVersion = std::move(packageVersion);
    }

    const std::string &SingerManifest::name() const {
        return _name;
    }

    void SingerManifest::setName(std::string name) {
        _name = std::move(name);
    }

    double SingerManifest::phonemeLength() const {
        return _phonemeLength;
    }

    void SingerManifest::setPhonemeLength(double length) {
        _phonemeLength = length;
    }

    const std::vector<LanguageInfo> &SingerManifest::languages() const {
        return _languages;
    }

    void SingerManifest::setLanguages(std::vector<LanguageInfo> languages) {
        _languages = std::move(languages);
    }

    const std::vector<SpeakerInfo> &SingerManifest::speakers() const {
        return _speakers;
    }

    void SingerManifest::setSpeakers(std::vector<SpeakerInfo> speakers) {
        _speakers = std::move(speakers);
    }

    const std::string &SingerManifest::defaultLanguage() const {
        return _defaultLanguage;
    }

    void SingerManifest::setDefaultLanguage(std::string defaultLanguage) {
        _defaultLanguage = std::move(defaultLanguage);
    }

    const std::vector<SingerImportInfo> &SingerManifest::imports() const {
        return _imports;
    }

    void SingerManifest::setImports(std::vector<SingerImportInfo> imports) {
        _imports = std::move(imports);
    }

    // ---------------------------------------------------------------------------
    // PackageManifest
    // ---------------------------------------------------------------------------

    const std::string &PackageManifest::packageId() const {
        return _packageId;
    }

    void PackageManifest::setPackageId(std::string packageId) {
        _packageId = std::move(packageId);
    }

    const std::filesystem::path &PackageManifest::rootPath() const {
        return _rootPath;
    }

    void PackageManifest::setRootPath(std::filesystem::path rootPath) {
        _rootPath = std::move(rootPath);
    }

    const std::vector<std::filesystem::path> &PackageManifest::singerRefs() const {
        return _singerRefs;
    }

    void PackageManifest::setSingerRefs(std::vector<std::filesystem::path> singerRefs) {
        _singerRefs = std::move(singerRefs);
    }

    const std::vector<std::filesystem::path> &PackageManifest::inferenceRefs() const {
        return _inferenceRefs;
    }

    void PackageManifest::setInferenceRefs(std::vector<std::filesystem::path> inferenceRefs) {
        _inferenceRefs = std::move(inferenceRefs);
    }

    stdc::VersionNumber PackageManifest::version() const {
        return _version;
    }

    void PackageManifest::setVersion(stdc::VersionNumber version) {
        _version = std::move(version);
    }

    const std::optional<stdc::VersionNumber> &PackageManifest::compatVersion() const {
        return _compatVersion;
    }

    void PackageManifest::setCompatVersion(std::optional<stdc::VersionNumber> compatVersion) {
        _compatVersion = std::move(compatVersion);
    }

    const std::string &PackageManifest::name() const {
        return _name;
    }

    void PackageManifest::setName(std::string name) {
        _name = std::move(name);
    }

    const std::string &PackageManifest::description() const {
        return _description;
    }

    void PackageManifest::setDescription(std::string description) {
        _description = std::move(description);
    }

    const std::string &PackageManifest::author() const {
        return _author;
    }

    void PackageManifest::setAuthor(std::string author) {
        _author = std::move(author);
    }

    const std::string &PackageManifest::license() const {
        return _license;
    }

    void PackageManifest::setLicense(std::string license) {
        _license = std::move(license);
    }

    const std::vector<std::string> &PackageManifest::dependencies() const {
        return _dependencies;
    }

    void PackageManifest::setDependencies(std::vector<std::string> dependencies) {
        _dependencies = std::move(dependencies);
    }

    const std::vector<SingerManifest> &PackageManifest::singers() const {
        return _singers;
    }

    void PackageManifest::setSingers(std::vector<SingerManifest> singers) {
        _singers = std::move(singers);
    }

    const std::vector<SpeakerInfo> &PackageManifest::speakers() const {
        return _speakers;
    }

    void PackageManifest::setSpeakers(std::vector<SpeakerInfo> speakers) {
        _speakers = std::move(speakers);
    }

    const std::vector<LanguageInfo> &PackageManifest::languages() const {
        return _languages;
    }

    void PackageManifest::setLanguages(std::vector<LanguageInfo> languages) {
        _languages = std::move(languages);
    }

    const std::vector<InferenceInfo> &PackageManifest::inferences() const {
        return _inferences;
    }

    void PackageManifest::setInferences(std::vector<InferenceInfo> inferences) {
        _inferences = std::move(inferences);
    }

}
