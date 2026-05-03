#include "InferenceContrib.h"

#include <fstream>
#include <set>

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
        Expected<void> readDesc(const std::filesystem::path &basePath, const JsonValue &pathValue);

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

    Expected<void> InferenceSpec::Impl::readDesc(const std::filesystem::path &basePath,
                                                 const JsonValue &pathValue) {
        if (!pathValue.isString()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid inference specification)",
            };
        }
        auto descPath = stdc::path::from_utf8(pathValue.toString());
        if (descPath.empty()) {
            return Error{
                Error::InvalidFormat,
                R"(inference specification path has invalid value)",
            };
        }
        if (descPath.is_relative()) {
            descPath = basePath / descPath;
        }

        auto obj = readJsonObjectFile(descPath, "inference");
        if (!obj) {
            return obj.error();
        }
        auto exp = read({}, obj.get());
        if (!exp) {
            return exp.error();
        }
        path = fs::canonical(descPath).parent_path();
        return Expected<void>();
    }

    Expected<void> InferenceSpec::Impl::read(const std::filesystem::path &basePath,
                                             const JsonObject &obj) {
        (void) basePath;
        stdc::VersionNumber fmtVersion_;
        std::string id_;
        std::string className_;

        DisplayText name_;
        int apiLevel_;

        JsonObject schema_;
        JsonObject configuration_;

        {
            const std::set<std::string_view> allowedKeys = {
                "$version", "class", "configuration", "id", "level", "name", "schema",
            };
            for (const auto &item : obj) {
                if (!allowedKeys.count(std::string_view(item.first))) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(unknown field "%1" in inference manifest)", item.first),
                    };
                }
            }
        }

        // Get attributes
        // $version
        {
            auto it = obj.find("$version");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "$version" field in inference manifest)",
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
                    R"(missing "id" field in inference manifest)",
                };
            }
            id_ = it->second.toString();
            if (!ContribLocator::isValidLocator(id_)) {
                return Error{
                    Error::InvalidFormat,
                    R"("id" field has invalid value in inference manifest)",
                };
            }
        }
        // class
        {
            auto it = obj.find("class");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "class" field in inference manifest)",
                };
            }
            className_ = it->second.toString();
            if (className_.empty()) {
                return Error{
                    Error::InvalidFormat,
                    R"("class" field has invalid value in inference manifest)",
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
                        stdc::formatN(R"("name" field has invalid value in inference manifest: %1)",
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
                    R"(missing "level" field in inference manifest)",
                };
            }
            apiLevel_ = it->second.toInt();
            if (apiLevel_ == 0) {
                return Error{
                    Error::InvalidFormat,
                    R"("level" field has invalid value in inference manifest)",
                };
            }
        }
        // schema
        {
            auto it = obj.find("schema");
            if (it != obj.end()) {
                if (!it->second.isObject()) {
                    return Error{
                        Error::InvalidFormat,
                        R"("schema" field has invalid value in inference manifest)",
                    };
                }
                schema_ = it->second.toObject();
            }
        }
        // configuration
        {
            auto it = obj.find("configuration");
            if (it != obj.end()) {
                if (!it->second.isObject()) {
                    return Error{
                        Error::InvalidFormat,
                        R"("configuration" field has invalid value in inference manifest)",
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
        if (!config.isString()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid inference specification)",
            };
        }
        auto spec = new InferenceSpec();
        auto spec_impl = static_cast<InferenceSpec::Impl *>(spec->_impl.get());
        if (auto exp = spec_impl->readDesc(basePath, config); !exp) {
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
