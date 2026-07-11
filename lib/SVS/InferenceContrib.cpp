#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Plugin/PluginFactory.h>
#include <synthrt/Core/Support/DisplayText.h>
#include <synthrt/Core/Support/Logging.h>

#include "../Core/Module/Module_p.h"

namespace srt::svs {

    static srt::core::LogCategory SVSLog("svs.inference");

    // ============================================================================
    // Helpers
    // ============================================================================

    // Each interpreter plugin type (acoustic, duration, pitch, variance, vocoder)
    // registers under its own iid; the inference config "class" field is the
    // plugin key (e.g. "ai.svs.AcousticInference"). Try all known interpreter
    // iids to locate the plugin by key.
    static InferenceInterpreterPlugin *
        findInterpreterPlugin(core::Runtime *rt, const std::string &className) {
        if (!rt || className.empty()) {
            return nullptr;
        }
        auto *plugins = rt->services().get<core::PluginFactory>();
        if (!plugins) {
            return nullptr;
        }
        static constexpr const char *knownIids[] = {
            "srt.svs.interpreter.acoustic",
            "srt.svs.interpreter.duration",
            "srt.svs.interpreter.pitch",
            "srt.svs.interpreter.variance",
            "srt.svs.interpreter.vocoder",
        };
        for (const char *iid : knownIids) {
            if (auto *p = plugins->plugin(iid, className.c_str())) {
                return static_cast<InferenceInterpreterPlugin *>(p);
            }
        }
        return nullptr;
    }

    // ============================================================================
    // InferenceSpec::Impl
    // ============================================================================

    class InferenceSpec::Impl : public core::ModuleSpec::Impl {
    public:
        Impl() : ModuleSpec::Impl("inference") {}
        ~Impl() override = default;

        // Extra fields beyond ModuleSpec::Impl.
        core::JsonObject manifestSchema;
        core::NO<InferenceSchema> schema;
        core::NO<InferenceConfiguration> inferenceConfiguration;
        core::DisplayText displayName;

        // Cached in loadSpec(Initialized); used by createImportOptions/createInference.
        core::NO<InferenceInterpreter> interpreter;
    };

    InferenceSpec::InferenceSpec() : core::ModuleSpec(*new Impl()) {
    }

    InferenceSpec::~InferenceSpec() = default;

    const std::string &InferenceSpec::className() const {
        return _impl->className;
    }

    core::DisplayText InferenceSpec::name() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.displayName;
    }

    int InferenceSpec::apiLevel() const {
        return _impl->apiLevel;
    }

    const core::JsonObject &InferenceSpec::manifestSchema() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.manifestSchema;
    }

    core::NO<InferenceSchema> InferenceSpec::schema() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.schema;
    }

    const core::JsonObject &InferenceSpec::manifestConfiguration() const {
        return _impl->manifestConfiguration;
    }

    core::NO<InferenceConfiguration> InferenceSpec::configuration() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.inferenceConfiguration;
    }

    const std::filesystem::path &InferenceSpec::path() const {
        return _impl->path;
    }

    core::Expected<core::NO<InferenceImportOptions>>
        InferenceSpec::createImportOptions(const core::JsonValue &options) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        if (!impl.interpreter) {
            return core::Error{
                core::Error::InvalidArgument,
                "InferenceSpec interpreter is not initialized (call loadSpec first)",
            };
        }
        return impl.interpreter->createImportOptions(this, options);
    }

    core::Expected<core::NO<Inference>>
        InferenceSpec::createInference(const core::NO<InferenceImportOptions> &importOptions,
                                       const core::NO<InferenceRuntimeOptions> &runtimeOptions) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        if (!impl.interpreter) {
            return core::Error{
                core::Error::InvalidArgument,
                "InferenceSpec interpreter is not initialized (call loadSpec first)",
            };
        }
        return impl.interpreter->createInference(this, importOptions, runtimeOptions);
    }

    // ============================================================================
    // InferenceCategory::Impl
    // ============================================================================

    class InferenceCategory::Impl : public core::ModuleCategory::Impl {
    public:
        Impl(InferenceCategory *decl, const std::string &name, core::Runtime *runtime)
            : core::ModuleCategory::Impl(decl, name, runtime) {}
        ~Impl() override = default;
    };

    InferenceCategory::InferenceCategory(core::Runtime *runtime)
        : core::ModuleCategory("inference", runtime) {
    }

    InferenceCategory::~InferenceCategory() = default;

    std::string InferenceCategory::key() const {
        return "inference";
    }

    std::string InferenceCategory::category() const {
        return "inference";
    }

    std::vector<InferenceSpec *> InferenceCategory::inferences() const {
        auto moduleSpecs = specs();
        std::vector<InferenceSpec *> result;
        result.reserve(moduleSpecs.size());
        for (auto *spec : moduleSpecs) {
            result.push_back(static_cast<InferenceSpec *>(spec));
        }
        return result;
    }

    std::vector<InferenceSpec *>
        InferenceCategory::findInferences(const core::ModuleLocator &identifier) const {
        auto moduleSpecs = findSpec(identifier);
        std::vector<InferenceSpec *> result;
        result.reserve(moduleSpecs.size());
        for (auto *spec : moduleSpecs) {
            result.push_back(static_cast<InferenceSpec *>(spec));
        }
        return result;
    }

    core::Expected<core::ModuleSpec *>
        InferenceCategory::parseSpec(const std::filesystem::path &basePath,
                                     const core::JsonValue &config) const {
        if (!config.isObject()) {
            return core::Error{
                core::Error::InvalidFormat,
                "inference config must be a JSON object",
            };
        }
        const auto &obj = config.toObject();

        auto *spec = new InferenceSpec();
        auto &impl = *static_cast<InferenceSpec::Impl *>(spec->_impl.get());

        // id (string)
        {
            auto it = obj.find("id");
            if (it != obj.end() && it->second.isString()) {
                impl.id = it->second.toString();
            }
        }
        // class (string) -> className
        {
            auto it = obj.find("class");
            if (it != obj.end() && it->second.isString()) {
                impl.className = it->second.toString();
            }
        }
        // level (int) -> apiLevel, default 1
        {
            auto it = obj.find("level");
            if (it != obj.end() && it->second.isInt()) {
                impl.apiLevel = static_cast<int>(it->second.toInt());
            } else {
                impl.apiLevel = 1;
            }
        }
        // configuration (object) -> manifestConfiguration
        {
            auto it = obj.find("configuration");
            if (it != obj.end() && it->second.isObject()) {
                impl.manifestConfiguration = it->second.toObject();
            }
        }
        // schema (object) -> manifestSchema
        {
            auto it = obj.find("schema");
            if (it != obj.end() && it->second.isObject()) {
                impl.manifestSchema = it->second.toObject();
            }
        }
        // name (string or object) -> displayName (optional)
        {
            auto it = obj.find("name");
            if (it != obj.end()) {
                if (auto exp = core::DisplayText::fromJsonValue(it->second)) {
                    impl.displayName = std::move(*exp);
                }
            }
        }

        impl.path = basePath;
        return spec;
    }

    core::Expected<void> InferenceCategory::loadSpec(core::ModuleSpec *spec,
                                                     core::ModuleSpec::State state) {
        // Delegate base state management to loadSpecBase (updates modules list, state).
        auto baseResult = loadSpecBase(spec, state);
        if (!baseResult) {
            return baseResult;
        }

        auto *infSpec = spec->as<InferenceSpec>();
        auto &impl = *static_cast<InferenceSpec::Impl *>(infSpec->_impl.get());

        switch (state) {
            case core::ModuleSpec::Initialized: {
                // Find the interpreter plugin via runtime and cache the interpreter.
                auto *rt = runtime();
                if (!rt) {
                    SVSLog.srtWarning("loadSpec(Initialized): runtime is null for inference "
                                      "spec class '%1', interpreter will be unavailable",
                                      impl.className);
                    break;
                }
                auto *plugin = findInterpreterPlugin(rt, impl.className);
                if (!plugin) {
                    // Plugin not found — log loudly so silent synthesis failures
                    // are diagnosable. The spec is still registered (Initialized)
                    // so callers can inspect metadata, but createInference() will
                    // return an error later.
                    SVSLog.srtWarning("loadSpec(Initialized): interpreter plugin not found "
                                      "for class '%1'; inference for this spec will fail "
                                      "at createInference time", impl.className);
                    break;
                }
                impl.interpreter = plugin->create();
                if (!impl.interpreter) {
                    SVSLog.srtWarning("loadSpec(Initialized): plugin->create() returned null "
                                      "for class '%1'", impl.className);
                }
                break;
            }

            case core::ModuleSpec::Ready: {
                if (!impl.interpreter) {
                    // Silent skip: no schema/configuration will be created. This
                    // is the continuation of the Initialized-phase failure above.
                    SVSLog.srtWarning("loadSpec(Ready): skipping schema/config creation "
                                      "for class '%1' (interpreter is null)", impl.className);
                    break;
                }
                // Create schema and configuration from the interpreter.
                if (auto exp = impl.interpreter->createSchema(infSpec)) {
                    impl.schema = std::move(*exp);
                } else {
                    return exp.takeError();
                }
                if (auto exp = impl.interpreter->createConfiguration(infSpec)) {
                    impl.inferenceConfiguration = std::move(*exp);
                } else {
                    return exp.takeError();
                }
                break;
            }

            case core::ModuleSpec::Finished:
            case core::ModuleSpec::Deleted:
                // Release cached interpreter and parsed objects.
                impl.interpreter.reset();
                impl.schema.reset();
                impl.inferenceConfiguration.reset();
                break;

            default:
                break;
        }
        return {};
    }

    // ============================================================================
    // Registrar
    // ============================================================================

    static core::ModuleCategoryRegistrar<InferenceCategory> g_inferenceRegistrar;

} // namespace srt::svs
