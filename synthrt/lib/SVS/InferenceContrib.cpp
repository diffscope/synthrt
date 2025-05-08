#include "InferenceContrib.h"

#include <fstream>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include "Contribute_p.h"
#include "InferenceInterpreter.h"
#include "InferenceInterpreterPlugin.h"

namespace fs = std::filesystem;

namespace srt {

    class InferenceSpec::Impl : public ContribSpec::Impl {
    public:
        Impl() : ContribSpec::Impl("inference") {
        }

        bool read(const std::filesystem::path &basePath, const JsonObject &obj,
                  Error *error) override;

        std::filesystem::path path;

        std::string className;

        DisplayText name;
        int apiLevel = 0;

        JsonObject manifestSchema;
        NO<InferenceSchema> schema;

        JsonObject manifestConfiguration;
        NO<InferenceConfiguration> configuration;

        InferenceInterpreter *interp = nullptr;
    };

    bool InferenceSpec::Impl::read(const std::filesystem::path &basePath, const JsonObject &obj,
                                   Error *error) {
        fs::path configPath;
        stdc::VersionNumber fmtVersion_;
        std::string id_;
        std::string className_;

        DisplayText name_;
        int apiLevel_;

        JsonObject schema_;
        JsonObject configuration_;

        // Parse desc
        {
            // id
            auto it = obj.find("id");
            if (it == obj.end()) {
                *error = {
                    Error::InvalidFormat,
                    R"(missing "id" field in inference contribute field)",
                };
                return false;
            }
            id_ = it->second.toString();
            if (!ContribLocator::isValidLocator(id_)) {
                *error = {
                    Error::InvalidFormat,
                    R"("id" field has invalid value in inference contribute field)",
                };
                return false;
            }

            // class
            it = obj.find("class");
            if (it == obj.end()) {
                *error = {
                    Error::InvalidFormat,
                    R"(missing "class" field in inference contribute field)",
                };
                return false;
            }
            className_ = it->second.toString();
            if (className_.empty()) {
                *error = {
                    Error::InvalidFormat,
                    R"("class" field has invalid value in inference contribute field)",
                };
                return false;
            }

            // configuration
            it = obj.find("configuration");
            if (it == obj.end()) {
                *error = {
                    Error::InvalidFormat,
                    R"(missing "configuration" field in inference contribute field)",
                };
                return false;
            }

            std::string configPathString = it->second.toString();
            if (configPathString.empty()) {
                *error = {
                    Error::InvalidFormat,
                    R"("configuration" field has invalid value in inference contribute field)",
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
                    stdc::formatN(R"(%1: failed to open inference manifest)", configPath),
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
                    stdc::formatN(R"(%1: invalid inference manifest format: %2)", configPath,
                                  error2),
                };
                return false;
            }
            if (!root.isObject()) {
                *error = {
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid inference manifest format)", configPath),
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
                    stdc::formatN(R"(%1: format version "%2" is not supported)", configPath,
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
        // level
        {
            auto it = configObj.find("level");
            if (it == configObj.end()) {
                *error = {
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: missing "level" field)", configPath),
                };
                return false;
            }
            apiLevel_ = it->second.toInt();
            if (apiLevel_ == 0) {
                *error = {
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: "level" field has invalid value)", configPath),
                };
                return false;
            }
        }
        // schema
        {
            auto it = configObj.find("schema");
            if (it != configObj.end()) {
                if (!it->second.isObject()) {
                    *error = {
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: "schema" field has invalid value)", configPath),
                    };
                    return false;
                }
                schema_ = it->second.toObject();
            }
        }
        // configuration
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
        fmtVersion = fmtVersion_;
        id = std::move(id_);
        className = std::move(className_);
        name = std::move(name_);
        apiLevel = apiLevel_;
        manifestSchema = std::move(schema_);
        manifestConfiguration = std::move(configuration_);
        return true;
    }

    class InferenceCategory::Impl : public ContribCategory::Impl {
    public:
        explicit Impl(InferenceCategory *decl, SynthUnit *su)
            : ContribCategory::Impl(decl, "inference", su) {
        }

        ~Impl() {
        }

        std::map<std::string, InferenceInterpreter *> interpreters;
    };



    InferenceSpec::~InferenceSpec() = default;

    const std::string &InferenceSpec::className() const {
        __stdc_impl_t;
        return impl.className;
    }

    DisplayText InferenceSpec::name() const {
        __stdc_impl_t;
        return impl.name;
    }

    int InferenceSpec::apiLevel() const {
        __stdc_impl_t;
        return impl.apiLevel;
    }

    const JsonObject &InferenceSpec::manifestSchema() const {
        __stdc_impl_t;
        return impl.manifestSchema;
    }

    NO<InferenceSchema> InferenceSpec::schema() const {
        __stdc_impl_t;
        return impl.schema;
    }

    const JsonObject &InferenceSpec::manifestConfiguration() const {
        __stdc_impl_t;
        return impl.manifestConfiguration;
    }

    NO<InferenceConfiguration> InferenceSpec::configuration() const {
        __stdc_impl_t;
        return impl.configuration;
    }

    std::filesystem::path InferenceSpec::path() const {
        __stdc_impl_t;
        return impl.path;
    }

    NO<InferenceImportOptions> InferenceSpec::createImportOptions(const JsonValue &options,
                                                                  Error *error) const {
        __stdc_impl_t;
        return impl.interp->createImportOptions(this, options, error);
    }

    Inference *InferenceSpec::createInference(const NO<InferenceImportOptions> &importOptions,
                                              const NO<InferenceRuntimeOptions> &runtimeOptions,
                                              Error *error) const {
        __stdc_impl_t;
        return impl.interp->createInference(this, importOptions, runtimeOptions, error);
    }

    InferenceSpec::InferenceSpec() : ContribSpec(*new Impl()) {
    }

    InferenceCategory::~InferenceCategory() = default;

    std::vector<InferenceSpec *>
        InferenceCategory::findInferences(const ContribLocator &locator) const {
        __stdc_impl_t;
        std::vector<InferenceSpec *> res;
        auto temp = impl.findContributes(locator);
        res.reserve(res.size());
        for (const auto &item : std::as_const(temp)) {
            res.push_back(static_cast<InferenceSpec *>(item));
        }
        return res;
    }

    std::vector<InferenceSpec *> InferenceCategory::inferences() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.su_mtx());
        std::vector<InferenceSpec *> res;
        res.reserve(impl.contributes.size());
        for (const auto &item : impl.contributes) {
            res.push_back(static_cast<InferenceSpec *>(item));
        }
        return res;
    }

    std::string InferenceCategory::key() const {
        return "inferences";
    }

    ContribSpec *InferenceCategory::parseSpec(const std::filesystem::path &basePath,
                                              const JsonValue &config, Error *error) const {
        __stdc_impl_t;
        if (!config.isObject()) {
            *error = {
                Error::InvalidFormat,
                R"(invalid inference specification)",
            };
            return nullptr;
        }
        auto spec = new InferenceSpec();
        if (!spec->_impl->read(basePath, config.toObject(), error)) {
            delete spec;
            return nullptr;
        }
        return spec;
    }

    bool InferenceCategory::loadSpec(ContribSpec *spec, ContribSpec::State state, Error *error) {
        __stdc_impl_t;
        switch (state) {
            case ContribSpec::Initialized: {
                auto spec1 = static_cast<InferenceSpec *>(spec);
                auto spec_impl = static_cast<InferenceSpec::Impl *>(spec1->_impl.get());

                const auto &key = spec1->className();
                InferenceInterpreter *interp = nullptr;

                // Search interpreter cache
                if (auto it = impl.interpreters.find(key); it != impl.interpreters.end()) {
                    interp = it->second;
                } else {
                    // Search interpreter
                    auto plugin =
                        SU()->plugin<InferenceInterpreterPlugin>(spec1->className().c_str());
                    if (!plugin) {
                        *error = {
                            Error::FeatureNotSupported,
                            stdc::formatN(
                                R"(required interpreter "%1" of inference "%2" not found)",
                                spec1->className(), spec1->id()),
                        };
                        return false;
                    }
                    interp = plugin->create();
                    impl.interpreters[key] = interp;
                }

                // Check api level
                if (interp->apiLevel() < spec1->apiLevel()) {
                    *error = {
                        Error::FeatureNotSupported,
                        stdc::formatN(
                            R"(required interpreter "%1" of api level %2 doesn't support inference "%3" of api level %4)",
                            spec1->className(), interp->apiLevel(), spec1->id(), spec1->apiLevel()),
                    };
                    return false;
                }

                // Create schema and configuration
                Error err1;
                NO<InferenceSchema> schema(interp->createSchema(spec1, &err1));
                if (!schema) {
                    *error = {
                        Error::InvalidFormat,
                        stdc::formatN(R"(inference "%1" validate schema failed: %2)", spec1->id(),
                                      err1.message()),
                    };
                }
                spec_impl->schema = schema;

                NO<InferenceConfiguration> config(interp->createConfiguration(spec1, &err1));
                if (!config) {
                    *error = {
                        Error::InvalidFormat,
                        stdc::formatN(R"(inference "%1" validate configuration failed: %2)",
                                      spec1->id(), err1.message()),
                    };
                }
                spec_impl->configuration = config;
                spec_impl->interp = interp;
                return ContribCategory::loadSpec(spec, state, error);
            }

            case ContribSpec::Ready:
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

    InferenceCategory::InferenceCategory(SynthUnit *su) : ContribCategory(*new Impl(this, su)) {
    }

    static ContribCategoryRegistrar<InferenceCategory> registrar;

}