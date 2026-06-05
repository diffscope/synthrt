#include "SingerContrib.h"

#include <fstream>
#include <cstdlib>
#include <regex>
#include <set>

#include <stdcorelib/3rdparty/llvm/smallvector.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/path.h>

#include "PackageRef.h"
#include "InferenceContrib.h"
#include "Contribute_p.h"
#include "SingerProviderPlugin.h"

namespace fs = std::filesystem;

namespace srt {

    static constexpr int kNumSingerImportFields = 5;

    class SingerImportData {
    public:
        ContribLocator inferenceLocator;
        InferenceSpec *inference;
        JsonValue manifestOptions;
        NO<InferenceImportOptions> options;
    };

    class SingerSpec::Impl : public ContribSpec::Impl {
    public:
        Impl() : ContribSpec::Impl("singer") {
        }

    public:
        Expected<void> read(const std::filesystem::path &basePath, const JsonObject &obj) override;

        std::filesystem::path path;

        std::string className;

        DisplayText name;
        int apiLevel = 0;

        DisplayPath avatar;
        DisplayPath background;
        llvm::SmallVector<SingerDemoAudio> demoAudios;

        llvm::SmallVector<SingerImportData, kNumSingerImportFields> importDataList;
        llvm::SmallVector<SingerImport, kNumSingerImportFields>
            importList; // wrapper of importDataList

        JsonObject manifestConfiguration;
        NO<SingerConfiguration> configuration;

        NO<SingerProvider> prov = nullptr;
    };

    static bool isValidPackageIdentifier(std::string_view token) {
        static const std::regex re(R"(^[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*$)");
        return std::regex_match(token.begin(), token.end(), re);
    }

    static Expected<JsonObject> readJsonObjectFile(const std::filesystem::path &path,
                                                   std::string_view displayName) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return Error{
                Error::FileNotOpen,
                stdc::formatN(R"(%1: failed to open %2 manifest)", path, displayName),
            };
        }

        std::stringstream ss;
        ss << file.rdbuf();

        std::string error;
        auto root = JsonValue::fromJson(ss.str(), true, &error);
        if (!error.empty()) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"(%1: invalid %2 manifest format: %3)", path, displayName, error),
            };
        }
        if (!root.isObject()) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"(%1: invalid %2 manifest format)", path, displayName),
            };
        }
        return root.toObject();
    }

    static bool readSingerImport(const JsonValue &val, SingerImportData *out,
                                 std::string *errorMessage) {
        if (!val.isObject()) {
            *errorMessage = R"(invalid data type)";
            return false;
        }
        auto obj = val.toObject();
        {
            const std::set<std::string_view> allowedKeys = {"id", "options", "inferenceId", "version"};
            for (const auto &item : obj) {
                if (!allowedKeys.count(std::string_view(item.first))) {
                    *errorMessage = stdc::formatN(R"(unknown field "%1")", item.first);
                    return false;
                }
            }
        }

        auto it = obj.find("inferenceId");
        if (it == obj.end()) {
            *errorMessage = R"(missing "inferenceId" field)";
            return false;
        }
        auto id = it->second.toString();
        if (!ContribLocator::isValidLocator(id)) {
            *errorMessage = R"("inferenceId" field has invalid value)";
            return false;
        }
        SingerImportData res;
        std::string package;
        stdc::VersionNumber version;

        it = obj.find("id");
        if (it != obj.end()) {
            package = it->second.toString();
            if (!isValidPackageIdentifier(package)) {
                *errorMessage = R"("id" field has invalid value)";
                return false;
            }
        }

        it = obj.find("version");
        if (it != obj.end()) {
            version = stdc::VersionNumber::fromString(it->second.toString());
            if (version.isEmpty()) {
                *errorMessage = R"("version" field has invalid value)";
                return false;
            }
        }

        res.inferenceLocator = ContribLocator(std::move(package), version, std::move(id));

        // options
        it = obj.find("options");
        if (it != obj.end()) {
            res.manifestOptions = it->second;
        }
        *out = std::move(res);
        return true;
    }

    static Expected<SingerDemoAudio> readDemoAudioItem(const JsonObject &obj) {
        {
            const std::set<std::string_view> allowedKeys = {"name", "path"};
            for (const auto &item : obj) {
                if (!allowedKeys.count(std::string_view(item.first))) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(unknown field "%1")", item.first),
                    };
                }
            }
        }

        auto it = obj.find("name");
        if (it == obj.end()) {
            return Error{
                Error::InvalidFormat,
                R"(missing "name" field)",
            };
        }
        auto name = DisplayText::fromJsonValue(it->second);
        if (!name) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"("name" field has invalid value: %1)", name.error().message()),
            };
        }

        it = obj.find("path");
        if (it == obj.end()) {
            return Error{
                Error::InvalidFormat,
                R"(missing "path" field)",
            };
        }
        auto path = DisplayPath::fromJsonValue(it->second);
        if (!path) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"("path" field has invalid value: %1)", path.error().message()),
            };
        }

        return SingerDemoAudio{name.take(), path.take()};
    }

    Expected<void> SingerSpec::Impl::read(const std::filesystem::path &basePath,
                                          const JsonObject &obj) {
        (void) basePath;
        stdc::VersionNumber fmtVersion_;
        std::string id_;
        std::string className_;

        DisplayText name_;
        int apiLevel_;

        DisplayPath avatar_;
        DisplayPath background_;
        llvm::SmallVector<SingerDemoAudio> demoAudios_;

        llvm::SmallVector<SingerImportData, kNumSingerImportFields> imports_;
        JsonObject configuration_;

        {
            const std::set<std::string_view> allowedKeys = {
                "$version",   "avatar",      "background", "class", "configuration",
                "demoAudio",  "id",          "imports",    "level", "name",
            };
            for (const auto &item : obj) {
                if (!allowedKeys.count(std::string_view(item.first))) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(unknown field "%1" in singer manifest)", item.first),
                    };
                }
            }
        }

        // $version
        {
            auto it = obj.find("$version");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "$version" field in singer manifest)",
                };
            }
            if (!it->second.isString() || it->second.toString() != "1.0") {
                return Error{
                    Error::FeatureNotSupported,
                    stdc::formatN(R"(format version "%1" is not supported)",
                                  it->second.toString()),
                };
            }
            fmtVersion_ = stdc::VersionNumber(1);
        }
        // id
        {
            auto it = obj.find("id");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "id" field in singer manifest)",
                };
            }
            id_ = it->second.toString();
            if (!ContribLocator::isValidLocator(id_)) {
                return Error{
                    Error::InvalidFormat,
                    R"("id" field has invalid value in singer manifest)",
                };
            }
        }
        // class
        {
            auto it = obj.find("class");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "class" field in singer manifest)",
                };
            }
            className_ = it->second.toString();
            if (className_.empty()) {
                return Error{
                    Error::InvalidFormat,
                    R"("class" field has invalid value in singer manifest)",
                };
            }
        }
        // name
        {
            auto it = obj.find("name");
            if (it != obj.end()) {
                auto exp = DisplayText::fromJsonValue(it->second);
                if (!exp) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"("name" field has invalid value in singer manifest: %1)",
                                      exp.error().message()),
                    };
                }
                name_ = exp.take();
            }
            if (name_.isEmpty()) {
                name_ = id_;
            }
        }
        // level
        {
            auto it = obj.find("level");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "level" field in singer manifest)",
                };
            }
            apiLevel_ = it->second.toInt();
            if (apiLevel_ == 0) {
                return Error{
                    Error::InvalidFormat,
                    R"("level" field has invalid value in singer manifest)",
                };
            }
        }
        // avatar
        {
            auto it = obj.find("avatar");
            if (it != obj.end()) {
                auto exp = DisplayPath::fromJsonValue(it->second);
                if (!exp) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"("avatar" field has invalid value in singer manifest: %1)",
                                      exp.error().message()),
                    };
                }
                avatar_ = exp.take();
            }
        }
        // background
        {
            auto it = obj.find("background");
            if (it != obj.end()) {
                auto exp = DisplayPath::fromJsonValue(it->second);
                if (!exp) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(
                            R"("background" field has invalid value in singer manifest: %1)",
                            exp.error().message()),
                    };
                }
                background_ = exp.take();
            }
        }
        // demoAudio
        {
            auto it = obj.find("demoAudio");
            if (it != obj.end()) {
                if (it->second.isArray()) {
                    for (const auto &item : it->second.toArray()) {
                        if (!item.isObject()) {
                            return Error{
                                Error::InvalidFormat,
                                stdc::formatN(
                                    R"("demoAudio" field entry %1 has invalid value in singer manifest)",
                                    demoAudios_.size() + 1),
                            };
                        }
                        auto exp = readDemoAudioItem(item.toObject());
                        if (!exp) {
                            return Error{
                                Error::InvalidFormat,
                                stdc::formatN(R"(invalid "demoAudio" field entry %1: %2)",
                                              demoAudios_.size() + 1, exp.error().message()),
                            };
                        }
                        demoAudios_.push_back(exp.take());
                    }
                } else {
                    auto exp = DisplayPath::fromJsonValue(it->second);
                    if (!exp) {
                        return Error{
                            Error::InvalidFormat,
                            stdc::formatN(
                                R"("demoAudio" field has invalid value in singer manifest: %1)",
                                exp.error().message()),
                        };
                    }
                    demoAudios_.push_back({DisplayText(), exp.take()});
                }
            }
        }
        // imports
        {
            auto it = obj.find("imports");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "imports" field in singer manifest)",
                };
            }
            if (!it->second.isArray()) {
                return Error{
                    Error::InvalidFormat,
                    R"("imports" field has invalid value in singer manifest)",
                };
            }

            for (const auto &item : it->second.toArray()) {
                SingerImportData singerImport;
                std::string errorMessage;
                if (!readSingerImport(item, &singerImport, &errorMessage)) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(invalid "imports" field entry %1: %2)",
                                      imports_.size() + 1, errorMessage),
                    };
                }
                imports_.push_back(singerImport);
            }
        }
        // misc
        {
            auto it = obj.find("configuration");
            if (it != obj.end()) {
                if (!it->second.isObject()) {
                    return Error{
                        Error::InvalidFormat,
                        R"("configuration" field has invalid value in singer manifest)",
                    };
                }
                configuration_ = it->second.toObject();
            }
        }

        fmtVersion = fmtVersion_;
        id = std::move(id_);
        className = std::move(className_);
        name = std::move(name_);
        apiLevel = apiLevel_;
        avatar = std::move(avatar_);
        background = std::move(background_);
        demoAudios = std::move(demoAudios_);
        importDataList = std::move(imports_);
        manifestConfiguration = std::move(configuration_);
        return Expected<void>();
    }

    static SingerImportData *staticEmptySingerImportData() {
        static SingerImportData empty;
        return &empty;
    }

    SingerImport::SingerImport() : _data(staticEmptySingerImportData()) {
    }

    SingerImport::~SingerImport() = default;

    bool SingerImport::isNull() const {
        return _data == staticEmptySingerImportData();
    }

    const ContribLocator &SingerImport::inferenceLocator() const {
        return _data->inferenceLocator;
    }

    InferenceSpec *SingerImport::inference() const {
        return _data->inference;
    }

    JsonValue SingerImport::manifestOptions() const {
        return _data->manifestOptions;
    }

    NO<InferenceImportOptions> SingerImport::options() const {
        return _data->options;
    }

    SingerImport::SingerImport(const SingerImportData *data) : _data(data) {
    }

    SingerSpec::~SingerSpec() = default;

    const std::string &SingerSpec::className() const {
        __stdc_impl_t;
        return impl.className;
    }

    DisplayText SingerSpec::name() const {
        __stdc_impl_t;
        return impl.name;
    }

    int SingerSpec::apiLevel() const {
        __stdc_impl_t;
        return impl.apiLevel;
    }

    DisplayPath SingerSpec::avatar() const {
        __stdc_impl_t;
        return impl.avatar;
    }

    DisplayPath SingerSpec::background() const {
        __stdc_impl_t;
        return impl.background;
    }

    stdc::array_view<SingerDemoAudio> SingerSpec::demoAudios() const {
        __stdc_impl_t;
        return impl.demoAudios;
    }

    stdc::array_view<SingerImport> SingerSpec::imports() const {
        __stdc_impl_t;
        return impl.importList;
    }

    const JsonObject &SingerSpec::manifestConfiguration() const {
        __stdc_impl_t;
        return impl.manifestConfiguration;
    }

    NO<SingerConfiguration> SingerSpec::configuration() const {
        __stdc_impl_t;
        return impl.configuration;
    }

    const std::filesystem::path &SingerSpec::path() const {
        __stdc_impl_t;
        return impl.path;
    }

    SingerSpec::SingerSpec() : ContribSpec(*new Impl()) {
    }


    class SingerCategory::Impl : public ContribCategory::Impl {
    public:
        explicit Impl(SingerCategory *decl, SynthUnit *su)
            : ContribCategory::Impl(decl, "singer", su) {
        }

        std::map<std::string, NO<SingerProvider>> providers;
    };

    SingerCategory::~SingerCategory() = default;

    std::vector<SingerSpec *> SingerCategory::findSingers(const ContribLocator &locator) const {
        __stdc_impl_t;
        std::vector<SingerSpec *> res;
        auto temp = impl.findContributes(locator);
        res.reserve(res.size());
        for (const auto &item : std::as_const(temp)) {
            res.push_back(static_cast<SingerSpec *>(item));
        }
        return res;
    }

    std::vector<SingerSpec *> SingerCategory::singers() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.su_mtx());
        std::vector<SingerSpec *> res;
        res.reserve(impl.contributes.size());
        for (const auto &item : impl.contributes) {
            res.push_back(static_cast<SingerSpec *>(item));
        }
        return res;
    }

    std::string SingerCategory::key() const {
        return "singers";
    }

    Expected<ContribSpec *> SingerCategory::parseSpec(const std::filesystem::path &basePath,
                                                      const JsonValue &config) const {
        if (!config.isString()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid singer specification)",
            };
        }
        auto descPath = stdc::path::from_utf8(config.toString());
        if (descPath.empty()) {
            return Error{
                Error::InvalidFormat,
                R"(singer specification path has invalid value)",
            };
        }
        if (descPath.is_relative()) {
            descPath = basePath / descPath;
        }

        auto obj = readJsonObjectFile(descPath, "singer");
        if (!obj) {
            return obj.error();
        }

        auto spec = new SingerSpec();
        if (auto exp = spec->_impl->read({}, obj.get()); !exp) {
            delete spec;
            return exp.error();
        }
        auto spec_impl = static_cast<SingerSpec::Impl *>(spec->_impl.get());
        spec_impl->path = fs::canonical(descPath).parent_path();
        return spec;
    }

    Expected<void> SingerCategory::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        __stdc_impl_t;
        switch (state) {
            case ContribSpec::Initialized: {
                auto singerSpec = static_cast<SingerSpec *>(spec);
                auto spec_impl = static_cast<SingerSpec::Impl *>(singerSpec->_impl.get());

                const auto &key = singerSpec->className();
                NO<SingerProvider> prov;

                // Search provider cache
                if (auto it = impl.providers.find(key); it != impl.providers.end()) {
                    prov = it->second;
                } else {
                    // Search provider
                    auto plugin =
                        SU()->plugin<SingerProviderPlugin>(singerSpec->className().c_str());
                    if (!plugin) {
                        return Error{
                            Error::FeatureNotSupported,
                            stdc::formatN(R"(required class "%1" of singer "%2" not found)",
                                          singerSpec->className(), singerSpec->id()),
                        };
                    }
                    prov = plugin->create();
                    impl.providers[key] = prov;
                }

                // Check api level
                if (prov->apiLevel() < singerSpec->apiLevel()) {
                    return Error{
                        Error::FeatureNotSupported,
                        stdc::formatN(
                            R"(required class "%1" of api level %2 doesn't support singer "%3" of api level %4)",
                            singerSpec->className(), prov->apiLevel(), singerSpec->id(),
                            singerSpec->apiLevel()),
                    };
                }

                // Create configuration
                auto config = prov->createConfiguration(singerSpec);
                if (!config) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(failed to parse singer configuration of "%1": %2)",
                                      singerSpec->id(), config.error().message()),
                    };
                }
                spec_impl->configuration = config.get();
                spec_impl->prov = prov;

                // Fix imports
                for (auto &imp : spec_impl->importDataList) {
                    auto &loc = imp.inferenceLocator;
                    auto package = loc.package();
                    auto version = loc.version();
                    if (package.empty()) {
                        package = spec->parent().id();
                        if (version.isEmpty()) {
                            version = spec->parent().version();
                        }
                    } else if (version.isEmpty()) {
                        for (const auto &dep : spec->parent().dependencies()) {
                            if (dep.id == package && (version.isEmpty() || dep.version > version)) {
                                version = dep.version;
                            }
                        }
                        if (version.isEmpty()) {
                            return Error{
                                Error::FeatureNotSupported,
                                stdc::formatN(
                                    R"(required package "%1" of singer "%2" is not declared in dependencies)",
                                    package, singerSpec->id()),
                            };
                        }
                    }
                    ContribLocator newLoc(std::move(package), version, loc.id());
                    loc = newLoc;
                }
                return ContribCategory::loadSpec(spec, state);
            }

            case ContribSpec::Ready: {
                // Check inferences
                auto spec1 = static_cast<SingerSpec *>(spec);
                auto spec_impl = static_cast<SingerSpec::Impl *>(spec1->_impl.get());
                auto inferenceReg = impl.su->category("inference")->as<InferenceCategory>();
                auto &importDataList = spec_impl->importDataList;
                for (auto &imp : importDataList) {
                    // Find inference
                    auto inferences = inferenceReg->findInferences(imp.inferenceLocator);
                    if (inferences.empty()) {
                        return Error{
                            Error::FeatureNotSupported,
                            stdc::formatN(R"(required inference "%1" of singer "%2" not found)",
                                          imp.inferenceLocator.toString(), spec1->id()),
                        };
                    }

                    // Create options
                    auto inference = inferences.front();
                    auto options = inference->createImportOptions(imp.manifestOptions);
                    if (!options) {
                        return Error{
                            Error::InvalidFormat,
                            stdc::formatN(
                                R"(failed to parse options of inference "%1" imported by singer "%2": %3)",
                                imp.inferenceLocator.toString(), spec1->id(),
                                options.error().message()),
                        };
                    }
                    imp.inference = inference;
                    imp.options = options.get();
                }

                llvm::SmallVector<SingerImport, kNumSingerImportFields> imports;
                imports.reserve(importDataList.size());
                for (const auto &imp : std::as_const(importDataList)) {
                    imports.push_back(SingerImport(&imp));
                }
                spec_impl->importList = std::move(imports);
                return Expected<void>();
            }

            case ContribSpec::Finished: {
                return Expected<void>();
            }

            case ContribSpec::Deleted: {
                return ContribCategory::loadSpec(spec, state);
            }
            default:
                break;
        }
        std::abort();
        return Expected<void>();
    }

    SingerCategory::SingerCategory(SynthUnit *su) : ContribCategory(*new Impl(this, su)) {
    }

    static ContribCategoryRegistrar<SingerCategory> registrar;

}
