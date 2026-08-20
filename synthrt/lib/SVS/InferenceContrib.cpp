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

    class InferenceSpec::Handler : public ContribSpecHandler {
    public:
        Expected<void> read(const std::filesystem::path &basePath, const JsonObject &obj);
        Expected<void> readDesc(const std::filesystem::path &basePath, const JsonValue &pathValue);

        std::filesystem::path path;

        std::string className;

        DisplayText name;
        int apiLevel = 0;

        JsonObject manifestSchema;
        UNO<InferenceSchema> schema;

        JsonObject manifestConfiguration;
        UNO<InferenceConfiguration> configuration;

        InferenceInterpreter *interp = nullptr;
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

        stdc::json::ParseError error;
        auto root = JsonValue::fromJson(ss.str(), true, &error);
        if (error) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"(%1: invalid %2 manifest format: %3)", path, displayName,
                              error.message()),
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

    Expected<void> InferenceSpec::Handler::readDesc(const std::filesystem::path &basePath,
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

    Expected<void> InferenceSpec::Handler::read(const std::filesystem::path &basePath,
                                             const JsonObject &obj) {
        (void) basePath;
        stdc::VersionNumber fmtVersion_;
        std::string className_;

        DisplayText name_;
        int apiLevel_;

        JsonObject schema_;
        JsonObject configuration_;

        {
            const std::set<std::string_view> allowedKeys = {
                "$version", "class", "configuration", "level", "name", "schema",
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
                    stdc::formatN(R"(format version "%1" is not supported)", it->second.toString()),
                };
            }
            fmtVersion_ = stdc::VersionNumber(1);
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
                auto exp =
                    DisplayText::fromJsonValue(it->second)
                        .withContext(Error::InvalidFormat,
                                     R"("name" field has invalid value in inference manifest)");
                if (!exp) {
                    return exp.error();
                }
                name_ = exp.take();
            }
            // Left empty when the manifest gives none. The identifier it falls back to is the
            // package's, which this file no longer knows, so name() fills it in instead.
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

        std::map<std::string, UNO<InferenceInterpreter>> interpreters;
    };



    InferenceSpec::~InferenceSpec() = default;

    const std::string &InferenceSpec::className() const {
        srt_handler_t;
        return handler.className;
    }

    DisplayText InferenceSpec::name() const {
        srt_handler_t;
        // A manifest that names itself nothing is displayed as whatever its package calls it.
        return handler.name.isEmpty() ? DisplayText(id()) : handler.name;
    }

    int InferenceSpec::apiLevel() const {
        srt_handler_t;
        return handler.apiLevel;
    }

    const JsonObject &InferenceSpec::manifestSchema() const {
        srt_handler_t;
        return handler.manifestSchema;
    }

    InferenceSchema *InferenceSpec::schema() const {
        srt_handler_t;
        return handler.schema.get();
    }

    const JsonObject &InferenceSpec::manifestConfiguration() const {
        srt_handler_t;
        return handler.manifestConfiguration;
    }

    InferenceConfiguration *InferenceSpec::configuration() const {
        srt_handler_t;
        return handler.configuration.get();
    }

    const std::filesystem::path &InferenceSpec::path() const {
        srt_handler_t;
        return handler.path;
    }

    Expected<NO<InferenceImportOptions>>
        InferenceSpec::createImportOptions(const JsonValue &options) const {
        srt_handler_t;
        return handler.interp->createImportOptions(this, options);
    }

    Expected<NO<Inference>>
        InferenceSpec::createInference(const NO<InferenceImportOptions> &importOptions,
                                       const NO<InferenceRuntimeOptions> &runtimeOptions) const {
        srt_handler_t;
        return handler.interp->createInference(this, importOptions, runtimeOptions);
    }

    InferenceSpec::InferenceSpec() : ContribSpec("inference", std::make_unique<Handler>()) {
    }

    InferenceCategory::~InferenceCategory() = default;

    std::vector<InferenceSpec *>
        InferenceCategory::findInferences(const ContribLocator &locator) const {
        stdc_impl_t;
        std::vector<InferenceSpec *> res;
        auto temp = impl.findContributes(locator);
        res.reserve(temp.size());
        for (const auto &item : std::as_const(temp)) {
            res.push_back(static_cast<InferenceSpec *>(item));
        }
        return res;
    }

    std::vector<InferenceSpec *> InferenceCategory::inferences() const {
        stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.su_mtx());
        std::vector<InferenceSpec *> res;
        res.reserve(impl.contributes.size());
        for (const auto &item : impl.contributes) {
            res.push_back(static_cast<InferenceSpec *>(item));
        }
        return res;
    }

    Expected<ContribSpec *> InferenceCategory::parseSpec(const std::filesystem::path &basePath,
                                                         const JsonValue &config) const {
        stdc_impl_t;
        if (!config.isString()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid inference specification)",
            };
        }
        auto spec = new InferenceSpec();
        auto spec_handler = static_cast<InferenceSpec::Handler *>(spec->handlerObject());
        if (auto exp = spec_handler->readDesc(basePath, config); !exp) {
            delete spec;
            return exp.error();
        }
        return spec;
    }

    Expected<void> InferenceCategory::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        stdc_impl_t;
        switch (state) {
            case ContribSpec::Initialized: {
                auto infSpec = static_cast<InferenceSpec *>(spec);
                auto spec_handler = static_cast<InferenceSpec::Handler *>(infSpec->handlerObject());

                const auto &key = infSpec->className();
                InferenceInterpreter *interp = nullptr;

                // Search interpreter cache
                if (auto it = impl.interpreters.find(key); it != impl.interpreters.end()) {
                    interp = it->second.get();
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
                    auto &slot = impl.interpreters[key];
                    slot = plugin->create();
                    interp = slot.get();
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
                auto schema = interp->createSchema(infSpec).withContext(
                    Error::InvalidFormat,
                    stdc::formatN(R"(failed to parse inference schema of "%1")", infSpec->id()));
                if (!schema) {
                    return schema.error();
                }
                spec_handler->schema = schema.take();

                auto config = interp->createConfiguration(infSpec).withContext(
                    Error::InvalidFormat,
                    stdc::formatN(R"(failed to parse inference configuration of "%1")",
                                  infSpec->id()));
                if (!config) {
                    return config.error();
                }
                spec_handler->configuration = config.take();
                spec_handler->interp = interp;
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

    static ContribCategoryRegistry::Add<ContribCategoryFactory<InferenceCategory>>
        registrar("inference", "Inference contributes");

}
