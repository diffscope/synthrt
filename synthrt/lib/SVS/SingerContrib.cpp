#include "SingerContrib.h"

#include <fstream>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/path.h>

#include "PackageRef.h"
#include "InferenceContrib.h"
#include "Contribute_p.h"

namespace fs = std::filesystem;

namespace srt {

    class SingerSpec::Impl : public ContribSpec::Impl {
    public:
        Impl() : ContribSpec::Impl("singer") {
        }

    public:
        bool read(const std::filesystem::path &basePath, const JsonObject &obj,
                  Error *error) override;

        std::filesystem::path path;

        DisplayText name;
        std::string model;

        std::filesystem::path avatar;
        std::filesystem::path background;
        std::filesystem::path demoAudio;

        std::vector<SingerImportData> importDataList;
        std::vector<SingerImport> importList; // wrapper of importDataList

        JsonObject configuration;
    };

    class SingerImportData {
    public:
        ContribLocator inferenceLocator;
        InferenceSpec *inference;
        JsonValue manifestOptions;
        NO<InferenceImportOptions> options;
    };

    static bool readSingerImport(const JsonValue &val, SingerImportData *out,
                                 std::string *errorMessage) {
        if (val.isString()) {
            auto inference = ContribLocator::fromString(val.toString());
            if (inference.id().empty()) {
                *errorMessage = R"(invalid id)";
                return false;
            }
            SingerImportData res;
            res.inferenceLocator = inference;
            *out = std::move(res);
            return true;
        }
        if (!val.isObject()) {
            *errorMessage = R"(invalid data type)";
            return false;
        }
        auto obj = val.toObject();
        auto it = obj.find("id");
        if (it == obj.end()) {
            *errorMessage = R"(missing "id" field)";
            return false;
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

    bool SingerSpec::Impl::read(const std::filesystem::path &basePath, const JsonObject &obj,
                                Error *error) {
        fs::path configPath;
        stdc::VersionNumber fmtVersion_;
        std::string id_;
        std::string model_;

        DisplayText name_;

        fs::path avatar_;
        fs::path background_;
        fs::path demoAudio_;

        std::vector<SingerImportData> imports_;
        JsonObject configuration_;

        // Parse desc
        {
            // id
            auto it = obj.find("id");
            if (it == obj.end()) {
                *error = {
                    Error::InvalidFormat,
                    R"(missing "id" field in singer contribute field)",
                };
                return false;
            }
            id_ = it->second.toString();
            if (!ContribLocator::isValidLocator(id_)) {
                *error = {
                    Error::InvalidFormat,
                    R"("id" field has invalid value in singer contribute field)",
                };
                return false;
            }

            // model
            it = obj.find("model");
            if (it == obj.end()) {
                *error = {
                    Error::InvalidFormat,
                    R"(missing "model" field in singer contribute field)",
                };
                return false;
            }
            model_ = it->second.toString();
            if (model_.empty()) {
                *error = {
                    Error::InvalidFormat,
                    R"("model" field has invalid value in singer contribute field)",
                };
                return false;
            }

            // path
            it = obj.find("path");
            if (it == obj.end()) {
                *error = {
                    Error::InvalidFormat,
                    R"(missing "path" field in singer contribute field)",
                };
                return false;
            }

            std::string configPathString = it->second.toString();
            if (configPathString.empty()) {
                *error = {
                    Error::InvalidFormat,
                    R"("path" field has invalid value in singer contribute field)",
                };
                return false;
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
                *error = {
                    Error::FileNotFound,
                    stdc::formatN(R"(%1: failed to open singer manifest)", configPath),
                };
                return false;
            }

            std::stringstream ss;
            ss << file.rdbuf();

            std::string error2;
            auto root = JsonValue::fromJson(ss.str(), true, &error2);
            if (!error2.empty()) {
                *error = {
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid singer manifest format: %2)", configPath, error2),
                };
                return false;
            }
            if (!root.isObject()) {
                *error = {
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid singer manifest format)", configPath),
                };
                return false;
            }
            configObj = root.toObject();
        }

        // Get attributes
        // $version
        {
            auto it = configObj.find("$version");
            if (it == configObj.end()) {
                *error = {
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: missing "$version" field)", configPath),
                };
                return false;
            }
            fmtVersion_ = stdc::VersionNumber::fromString(it->second.toString());
            if (fmtVersion_ > stdc::VersionNumber(1)) {
                *error = {
                    Error::FeatureNotSupported,
                    stdc::formatN(R"(%1: format version "%1" is not supported)",
                                  fmtVersion_.toString()),
                };
                return false;
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
                    *error = {
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: "imports" field has invalid value)", configPath),
                    };
                    return false;
                }

                for (const auto &item : it->second.toArray()) {
                    SingerImportData singerImport;
                    std::string errorMessage;
                    if (!readSingerImport(item, &singerImport, &errorMessage)) {
                        *error = {
                            Error::InvalidFormat,
                            stdc::formatN(R"(%1: invalid "imports" field entry %2: %3)", configPath,
                                          imports_.size() + 1, errorMessage),
                        };
                        return false;
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
                    *error = {
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: "configuration" field has invalid value)", configPath),
                    };
                    return false;
                }
                configuration_ = it->second.toObject();
            }
        }

        path = fs::canonical(configPath).parent_path();
        id = std::move(id_);
        model = std::move(model_);
        name = std::move(name_);
        avatar = std::move(avatar_);
        background = std::move(background_);
        demoAudio = std::move(demoAudio_);
        importDataList = std::move(imports_);
        configuration = std::move(configuration_);
        return true;
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

    const std::string &SingerSpec::model() const {
        __stdc_impl_t;
        return impl.model;
    }

    DisplayText SingerSpec::name() const {
        __stdc_impl_t;
        return impl.name;
    }

    std::filesystem::path SingerSpec::avatar() const {
        __stdc_impl_t;
        return impl.avatar;
    }

    std::filesystem::path SingerSpec::background() const {
        __stdc_impl_t;
        return impl.background;
    }

    std::filesystem::path SingerSpec::demoAudio() const {
        __stdc_impl_t;
        return impl.demoAudio;
    }

    stdc::array_view<SingerImport> SingerSpec::imports() const {
        __stdc_impl_t;
        return impl.importList;
    }

    const JsonObject &SingerSpec::configuration() const {
        __stdc_impl_t;
        return impl.configuration;
    }

    std::filesystem::path SingerSpec::path() const {
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

    ContribSpec *SingerCategory::parseSpec(const std::filesystem::path &basePath,
                                           const JsonValue &config, Error *error) const {
        if (!config.isObject()) {
            *error = {
                Error::InvalidFormat,
                R"(invalid inference specification)",
            };
            return nullptr;
        }
        auto spec = new SingerSpec();
        if (!spec->_impl->read(basePath, config.toObject(), error)) {
            delete spec;
            return nullptr;
        }
        return spec;
    }

    bool SingerCategory::loadSpec(ContribSpec *spec, ContribSpec::State state, Error *error) {
        __stdc_impl_t;
        switch (state) {
            case ContribSpec::Initialized: {
                // Fix imports
                auto singerSpec = static_cast<SingerSpec *>(spec);
                auto spec_d = static_cast<SingerSpec::Impl *>(singerSpec->_impl.get());
                for (auto &imp : spec_d->importDataList) {
                    ContribLocator newLocator(
                        imp.inferenceLocator.package().empty() ? spec->parent().id()
                                                               : imp.inferenceLocator.package(),
                        imp.inferenceLocator.version().isEmpty() ? spec->parent().version()
                                                                 : imp.inferenceLocator.version(),
                        imp.inferenceLocator.id());
                    imp.inferenceLocator = newLocator;
                }
                return ContribCategory::loadSpec(spec, state, error);
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
                        *error = {
                            Error::FeatureNotSupported,
                            stdc::formatN(R"(required inference "%1" of singer "%2" not found)",
                                          imp.inferenceLocator.toString(), spec1->id()),
                        };
                        return false;
                    }

                    // Create options
                    auto inference = inferences.front();
                    Error err1;
                    auto options = inference->createImportOptions(imp.manifestOptions, &err1);
                    if (!options) {
                        *error = {
                            Error::InvalidFormat,
                            stdc::formatN(
                                R"(failed to parse options of inference "%1" imported by singer "%2": %3)",
                                imp.inferenceLocator.toString(), spec1->id(), err1.message()),
                        };
                        return false;
                    }
                    imp.inference = inference;
                    imp.options = options;
                }

                std::vector<SingerImport> imports;
                imports.reserve(importDataList.size());
                for (const auto &imp : std::as_const(importDataList)) {
                    imports.push_back(SingerImport(&imp));
                }
                spec_impl->importList = std::move(imports);
                return true;
            }

            case ContribSpec::Finished: {
                return true;
            }

            case ContribSpec::Deleted: {
                return ContribCategory::loadSpec(spec, state, error);
            }
            default:
                break;
        }
        return false;
    }

    SingerCategory::SingerCategory(SynthUnit *su) : ContribCategory(*new Impl(this, su)) {
    }

    static ContribCategoryRegistrar<SingerCategory> registrar;

}