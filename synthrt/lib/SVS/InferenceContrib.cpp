#include "InferenceContrib.h"

#include <fstream>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include "Inference.h"
#include "InferenceInterpreter.h"
#include "InferenceInterpreterPlugin.h"
#include "Contribute_p.h"

namespace fs = std::filesystem;

namespace srt {

    class InferenceSpec::Impl : public ContribSpec::Impl {
    public:
        Impl() : ContribSpec::Impl("inference") {
        }

        Expected<void> read(const std::filesystem::path &basePath, const JsonObject &obj) override;

        std::filesystem::path path;

        std::string className;

        DisplayText name;
        int apiLevel = 0;

        JsonObject manifestSchema;
        NO<InferenceSchema> schema;

        JsonObject manifestConfiguration;
        NO<InferenceConfiguration> configuration;

        NO<InferenceInterpreter> interp = nullptr;
    };

    Expected<void> InferenceSpec::Impl::read(const std::filesystem::path &basePath,
                                             const JsonObject &obj) {
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
                return Error{
                    Error::InvalidFormat,
                    R"(missing "id" field in inference contribute field)",
                };
            }
            id_ = it->second.toString();
            if (!ContribLocator::isValidLocator(id_)) {
                return Error{
                    Error::InvalidFormat,
                    R"("id" field has invalid value in inference contribute field)",
                };
            }

            // class
            it = obj.find("class");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "class" field in inference contribute field)",
                };
            }
            className_ = it->second.toString();
            if (className_.empty()) {
                return Error{
                    Error::InvalidFormat,
                    R"("class" field has invalid value in inference contribute field)",
                };
            }

            // configuration
            it = obj.find("configuration");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "configuration" field in inference contribute field)",
                };
            }

            std::string configPathString = it->second.toString();
            if (configPathString.empty()) {
                return Error{
                    Error::InvalidFormat,
                    R"("configuration" field has invalid value in inference contribute field)",
                };
            }

            configPath = stdc::path::from_utf8(configPathString);
            if (auto configPathExtension = stdc::to_lower(configPath.extension().string());
                configPathExtension != ".json") {
                configPath += ".json";
            }
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
                    stdc::formatN(R"(%1: failed to open inference manifest)", configPath),
                };
            }

            std::stringstream ss;
            ss << file.rdbuf();

            std::string error2;
            auto root = JsonValue::fromJson(ss.str(), true, &error2);
            if (!error2.empty()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid inference manifest format: %2)", configPath,
                                  error2),
                };
            }
            if (!root.isObject()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid inference manifest format)", configPath),
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
                        stdc::formatN(R"(%1: format version "%2" is not supported)", configPath,
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
        // schema
        {
            auto it = configObj.find("schema");
            if (it != configObj.end()) {
                if (!it->second.isObject()) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: "schema" field has invalid value)", configPath),
                    };
                }
                schema_ = it->second.toObject();
            }
        }
        // configuration
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
        fmtVersion = fmtVersion_;
        id = std::move(id_);
        className = std::move(className_);
        name = std::move(name_);
        apiLevel = apiLevel_;
        manifestSchema = std::move(schema_);
        manifestConfiguration = std::move(configuration_);
        return Expected<void>();
    }

    class InferenceCategory::Impl : public ContribCategory::Impl {
    public:
        explicit Impl(InferenceCategory *decl, SynthUnit *su)
            : ContribCategory::Impl(decl, "inference", su) {
        }

        ~Impl() {
        }

        std::map<std::string, NO<InferenceInterpreter>> interpreters;
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

    const std::filesystem::path &InferenceSpec::path() const {
        __stdc_impl_t;
        return impl.path;
    }

    Expected<NO<InferenceImportOptions>>
        InferenceSpec::createImportOptions(const JsonValue &options) const {
        __stdc_impl_t;
        return impl.interp->createImportOptions(this, options);
    }

    Expected<NO<Inference>>
        InferenceSpec::createInference(const NO<InferenceImportOptions> &importOptions,
                                       const NO<InferenceRuntimeOptions> &runtimeOptions) const {
        __stdc_impl_t;
        return impl.interp->createInference(this, importOptions, runtimeOptions);
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

    Expected<ContribSpec *> InferenceCategory::parseSpec(const std::filesystem::path &basePath,
                                                         const JsonValue &config) const {
        __stdc_impl_t;
        if (!config.isObject()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid inference specification)",
            };
        }
        auto spec = new InferenceSpec();
        if (auto exp = spec->_impl->read(basePath, config.toObject()); !exp) {
            delete spec;
            return exp.error();
        }
        return spec;
    }

    Expected<void> InferenceCategory::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        __stdc_impl_t;
        switch (state) {
            case ContribSpec::Initialized: {
                auto infSpec = static_cast<InferenceSpec *>(spec);
                auto spec_impl = static_cast<InferenceSpec::Impl *>(infSpec->_impl.get());

                const auto &key = infSpec->className();
                NO<InferenceInterpreter> interp;

                // Search interpreter cache
                if (auto it = impl.interpreters.find(key); it != impl.interpreters.end()) {
                    interp = it->second;
                } else {
                    // Search interpreter
                    auto plugin =
                        SU()->plugin<InferenceInterpreterPlugin>(infSpec->className().c_str());
                    if (!plugin) {
                        return Error{
                            Error::FeatureNotSupported,
                            stdc::formatN(
                                R"(required interpreter "%1" of inference "%2" not found)",
                                infSpec->className(), infSpec->id()),
                        };
                    }
                    interp = plugin->create();
                    impl.interpreters[key] = interp;
                }

                // Check api level
                if (interp->apiLevel() < infSpec->apiLevel()) {
                    return Error{
                        Error::FeatureNotSupported,
                        stdc::formatN(
                            R"(required interpreter "%1" of api level %2 doesn't support inference "%3" of api level %4)",
                            infSpec->className(), interp->apiLevel(), infSpec->id(),
                            infSpec->apiLevel()),
                    };
                }

                // Create schema and configuration
                auto schema = interp->createSchema(infSpec);
                if (!schema) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(failed to parse inference schema of "%1": %2)",
                                      infSpec->id(), schema.error().message()),
                    };
                }
                spec_impl->schema = schema.get();

                auto config = interp->createConfiguration(infSpec);
                if (!config) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(failed to parse inference configuration of "%1": %2)",
                                      infSpec->id(), config.error().message()),
                    };
                }
                spec_impl->configuration = config.get();
                spec_impl->interp = interp;
                return ContribCategory::loadSpec(spec, state);
            }

            case ContribSpec::Ready:
            case ContribSpec::Finished: {
                return Expected<void>();
            }

            case ContribSpec::Deleted: {
                return ContribCategory::loadSpec(spec, state);
            }
            default:
                break;
        }
        return Expected<void>();
    }

    InferenceCategory::InferenceCategory(SynthUnit *su) : ContribCategory(*new Impl(this, su)) {
    }

    static ContribCategoryRegistrar<InferenceCategory> registrar;

}