#include "SingerContrib.h"

#include <fstream>
#include <cstdlib>

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

        std::string arch;

        DisplayText name;
        int apiLevel = 0;

        std::filesystem::path avatar;
        std::filesystem::path background;
        std::filesystem::path demoAudio;

        llvm::SmallVector<SingerImportData, kNumSingerImportFields> importDataList;
        llvm::SmallVector<SingerImport, kNumSingerImportFields>
            importList; // wrapper of importDataList

        JsonObject manifestConfiguration;
        NO<SingerConfiguration> configuration;

        NO<SingerProvider> prov = nullptr;
    };

    static bool readSingerImport(const JsonValue &val, SingerImportData *out,
                                 std::string *errorMessage) {
        if (val.isString()) {
            auto inference = ContribLocator::fromString(val.toString());
            if (inference.id().empty()) {
                *errorMessage = R"(invalid id)";
            }
            SingerImportData res;
            res.inferenceLocator = inference;
            *out = std::move(res);
            return true;
        }
        if (!val.isObject()) {
            *errorMessage = R"(invalid data type)";
        }
        auto obj = val.toObject();
        auto it = obj.find("id");
        if (it == obj.end()) {
            *errorMessage = R"(missing "id" field)";
        }
        auto inference = ContribLocator::fromString(it->second.toString());
        SingerImportData res;
        res.inferenceLocator = inference;

        // options
        it = obj.find("options");
        if (it != obj.end()) {
            res.manifestOptions = it->second;
        }
        *out = std::move(res);
        return true;
    }

    Expected<void> SingerSpec::Impl::read(const std::filesystem::path &basePath,
                                          const JsonObject &obj) {
        fs::path configPath;
        stdc::VersionNumber fmtVersion_;
        std::string id_;
        std::string arch_;

        DisplayText name_;
        int apiLevel_;

        fs::path avatar_;
        fs::path background_;
        fs::path demoAudio_;

        llvm::SmallVector<SingerImportData, kNumSingerImportFields> imports_;
        JsonObject configuration_;

        // Parse desc
        {
            // id
            auto it = obj.find("id");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "id" field in singer contribute field)",
                };
            }
            id_ = it->second.toString();
            if (!ContribLocator::isValidLocator(id_)) {
                return Error{
                    Error::InvalidFormat,
                    R"("id" field has invalid value in singer contribute field)",
                };
            }

            // arch
            it = obj.find("arch");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "arch" field in singer contribute field)",
                };
            }
            arch_ = it->second.toString();
            if (arch_.empty()) {
                return Error{
                    Error::InvalidFormat,
                    R"("arch" field has invalid value in singer contribute field)",
                };
            }

            // path
            it = obj.find("path");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "path" field in singer contribute field)",
                };
            }

            std::string configPathString = it->second.toString();
            if (configPathString.empty()) {
                return Error{
                    Error::InvalidFormat,
                    R"("path" field has invalid value in singer contribute field)",
                };
            }

            configPath = stdc::path::from_utf8(configPathString);
            if (configPath.is_relative()) {
                configPath = basePath / configPath;
            }
        }

        // Read configuration
        JsonObject configObj;
        {
            std::ifstream file(configPath);
            if (!file.is_open()) {
                return Error{
                    Error::FileNotFound,
                    stdc::formatN(R"(%1: failed to open singer manifest)", configPath),
                };
            }

            std::stringstream ss;
            ss << file.rdbuf();

            std::string error2;
            auto root = JsonValue::fromJson(ss.str(), true, &error2);
            if (!error2.empty()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid singer manifest format: %2)", configPath, error2),
                };
            }
            if (!root.isObject()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid singer manifest format)", configPath),
                };
            }
            configObj = root.toObject();
        }

        // Get attributes
        // $version
        {
            auto it = configObj.find("$version");
            if (it == configObj.end()) {
                fmtVersion_ = stdc::VersionNumber(1);
            } else {
                fmtVersion_ = stdc::VersionNumber::fromString(it->second.toString());
                if (fmtVersion_ > stdc::VersionNumber(1)) {
                    return Error{
                        Error::FeatureNotSupported,
                        stdc::formatN(R"(%1: format version "%1" is not supported)",
                                      fmtVersion_.toString()),
                    };
                }
            }
        }
        // name
        {
            auto it = configObj.find("name");
            if (it != configObj.end()) {
                name_ = it->second;
            }
            if (name_.isEmpty()) {
                name_ = id_;
            }
        }
        // level
        {
            auto it = configObj.find("level");
            if (it == configObj.end()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: missing "level" field)", configPath),
                };
            }
            apiLevel_ = it->second.toInt();
            if (apiLevel_ == 0) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: "level" field has invalid value)", configPath),
                };
            }
        }
        // avatar
        {
            auto it = configObj.find("avatar");
            if (it != configObj.end()) {
                avatar_ = stdc::path::from_utf8(it->second.toString());
            }
        }
        // background
        {
            auto it = configObj.find("background");
            if (it != configObj.end()) {
                background_ = stdc::path::from_utf8(it->second.toString());
            }
        }
        // demoAudio
        {
            auto it = configObj.find("demoAudio");
            if (it != configObj.end()) {
                demoAudio_ = stdc::path::from_utf8(it->second.toString());
            }
        }
        // imports
        {
            auto it = configObj.find("imports");
            if (it != configObj.end()) {
                if (!it->second.isArray()) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: "imports" field has invalid value)", configPath),
                    };
                }

                for (const auto &item : it->second.toArray()) {
                    SingerImportData singerImport;
                    std::string errorMessage;
                    if (!readSingerImport(item, &singerImport, &errorMessage)) {
                        return Error{
                            Error::InvalidFormat,
                            stdc::formatN(R"(%1: invalid "imports" field entry %2: %3)", configPath,
                                          imports_.size() + 1, errorMessage),
                        };
                    }
                    imports_.push_back(singerImport);
                }
            }
        }
        // misc
        {
            auto it = configObj.find("configuration");
            if (it != configObj.end()) {
                if (!it->second.isObject()) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: "configuration" field has invalid value)", configPath),
                    };
                }
                configuration_ = it->second.toObject();
            }
        }

        path = fs::canonical(configPath).parent_path();
        id = std::move(id_);
        arch = std::move(arch_);
        name = std::move(name_);
        apiLevel = apiLevel_;
        avatar = std::move(avatar_);
        background = std::move(background_);
        demoAudio = std::move(demoAudio_);
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

    const std::string &SingerSpec::arch() const {
        __stdc_impl_t;
        return impl.arch;
    }

    DisplayText SingerSpec::name() const {
        __stdc_impl_t;
        return impl.name;
    }

    int SingerSpec::apiLevel() const {
        __stdc_impl_t;
        return impl.apiLevel;
    }

    const std::filesystem::path &SingerSpec::avatar() const {
        __stdc_impl_t;
        return impl.avatar;
    }

    const std::filesystem::path &SingerSpec::background() const {
        __stdc_impl_t;
        return impl.background;
    }

    const std::filesystem::path &SingerSpec::demoAudio() const {
        __stdc_impl_t;
        return impl.demoAudio;
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
        if (!config.isObject()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid inference specification)",
            };
        }
        auto spec = new SingerSpec();
        if (auto exp = spec->_impl->read(basePath, config.toObject()); !exp) {
            delete spec;
            return exp.error();
        }
        return spec;
    }

    Expected<void> SingerCategory::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        __stdc_impl_t;
        switch (state) {
            case ContribSpec::Initialized: {
                auto singerSpec = static_cast<SingerSpec *>(spec);
                auto spec_impl = static_cast<SingerSpec::Impl *>(singerSpec->_impl.get());

                const auto &key = singerSpec->arch();
                NO<SingerProvider> prov;

                // Search provider cache
                if (auto it = impl.providers.find(key); it != impl.providers.end()) {
                    prov = it->second;
                } else {
                    // Search provider
                    auto plugin = SU()->plugin<SingerProviderPlugin>(singerSpec->arch().c_str());
                    if (!plugin) {
                        return Error{
                            Error::FeatureNotSupported,
                            stdc::formatN(R"(required arch "%1" of singer "%2" not found)",
                                          singerSpec->arch(), singerSpec->id()),
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
                            R"(required arch "%1" of api level %2 doesn't support singer "%3" of api level %4)",
                            singerSpec->arch(), prov->apiLevel(), singerSpec->id(),
                            singerSpec->apiLevel()),
                    };
                }

                // Create configuration
                auto config = prov->createConfiguration(singerSpec);
                if (!config) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(failed to parse inference configuration of "%1": %2)",
                                      singerSpec->id(), config.error().message()),
                    };
                }
                spec_impl->configuration = config.get();
                spec_impl->prov = prov;

                // Fix imports
                for (auto &imp : spec_impl->importDataList) {
                    auto &loc = imp.inferenceLocator;
                    ContribLocator newLoc(
                        loc.package().empty() ? spec->parent().id() : loc.package(),
                        loc.version().isEmpty() ? spec->parent().version() : loc.version(),
                        loc.id());
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