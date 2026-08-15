#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/SingerProvider.h>
#include <synthrt/SVS/SingerProviderPlugin.h>
#include <synthrt/SVS/InferenceContrib.h>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/DisplayText.h>
#include <synthrt/Core/Support/Logging.h>

#include "../Core/Module/Module_p.h"

namespace srt::svs {

    static core::LogCategory SVSLog("svs.singer");

    // ============================================================================
    // SingerImport
    // ============================================================================

    SingerImport::SingerImport() = default;
    SingerImport::~SingerImport() = default;

    bool SingerImport::isNull() const {
        return _inference == nullptr;
    }

    InferenceSpec *SingerImport::inference() const {
        return _inference;
    }

    core::JsonValue SingerImport::manifestOptions() const {
        return _manifestOptions;
    }

    core::NO<InferenceImportOptions> SingerImport::options() const {
        return _options;
    }

    // ============================================================================
    // SingerSpec::Impl
    // ============================================================================

    class SingerSpec::Impl : public core::ModuleSpec::Impl {
    public:
        Impl() : ModuleSpec::Impl("singer") {}
        ~Impl() override = default;

        core::DisplayText displayName;
        core::NO<SingerConfiguration> singerConfiguration;
        std::vector<SingerImport> imports;

        // Temporary storage for inferenceId references parsed from JSON;
        // consumed in loadSpec(Initialized) when resolving InferenceSpec pointers.
        // importInferenceIds[i] corresponds to imports[i].
        std::vector<std::string> importInferenceIds;
    };

    SingerSpec::SingerSpec() : core::ModuleSpec(*new Impl()) {
    }

    SingerSpec::~SingerSpec() = default;

    const std::string &SingerSpec::className() const {
        return _impl->m_className;
    }

    core::DisplayText SingerSpec::name() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.displayName;
    }

    int SingerSpec::apiLevel() const {
        return _impl->m_apiLevel;
    }

    const core::JsonObject &SingerSpec::manifestConfiguration() const {
        return _impl->m_manifestConfiguration;
    }

    core::NO<SingerConfiguration> SingerSpec::configuration() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.singerConfiguration;
    }

    const std::vector<SingerImport> &SingerSpec::imports() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.imports;
    }

    const std::filesystem::path &SingerSpec::path() const {
        return _impl->m_path;
    }

    // ============================================================================
    // SingerCategory::Impl
    // ============================================================================

    class SingerCategory::Impl : public core::ModuleCategory::Impl {
    public:
        Impl(SingerCategory *decl, const std::string &name, core::Runtime *runtime)
            : core::ModuleCategory::Impl(decl, name, runtime) {}
        ~Impl() override = default;
    };

    SingerCategory::SingerCategory(core::Runtime *runtime)
        : core::ModuleCategory("singer", runtime) {
    }

    SingerCategory::~SingerCategory() = default;

    std::string SingerCategory::key() const {
        return "singer";
    }

    std::string SingerCategory::category() const {
        return "singer";
    }

    std::vector<SingerSpec *> SingerCategory::singers() const {
        auto moduleSpecs = specs();
        std::vector<SingerSpec *> result;
        result.reserve(moduleSpecs.size());
        for (auto *spec : moduleSpecs) {
            result.push_back(static_cast<SingerSpec *>(spec));
        }
        return result;
    }

    std::vector<SingerSpec *>
        SingerCategory::findSingers(const core::ModuleLocator &locator) const {
        auto moduleSpecs = findSpec(locator);
        std::vector<SingerSpec *> result;
        result.reserve(moduleSpecs.size());
        for (auto *spec : moduleSpecs) {
            result.push_back(static_cast<SingerSpec *>(spec));
        }
        return result;
    }

    core::Expected<core::ModuleSpec *>
        SingerCategory::parseSpec(const std::filesystem::path &basePath,
                                  const core::JsonValue &config) const {
        if (!config.isObject()) {
            return core::Error{
                core::Error::InvalidFormat,
                "singer config must be a JSON object",
            };
        }
        const auto &obj = config.toObject();

        auto *spec = new SingerSpec();
        auto &impl = *static_cast<SingerSpec::Impl *>(spec->_impl.get());

        // id (string)
        {
            auto it = obj.find("id");
            if (it != obj.end() && it->second.isString()) {
                impl.m_id = it->second.toString();
            }
        }
        // class (string, optional) -> className. Voice bank singers usually
        // don't have a class field; leave empty when absent.
        {
            auto it = obj.find("class");
            if (it != obj.end() && it->second.isString()) {
                impl.m_className = it->second.toString();
            }
        }
        // level (int) -> apiLevel, default 1
        {
            auto it = obj.find("level");
            if (it != obj.end() && it->second.isInt()) {
                impl.m_apiLevel = static_cast<int>(it->second.toInt());
            } else {
                impl.m_apiLevel = 1;
            }
        }
        // configuration (object) -> manifestConfiguration
        {
            auto it = obj.find("configuration");
            if (it != obj.end() && it->second.isObject()) {
                impl.m_manifestConfiguration = it->second.toObject();
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
        // imports (array of {id/inferenceId, options}) -> imports + importInferenceIds
        {
            auto it = obj.find("imports");
            if (it != obj.end() && it->second.isArray()) {
                const auto &arr = it->second.toArray();
                impl.imports.reserve(arr.size());
                impl.importInferenceIds.reserve(arr.size());
                for (const auto &item : arr) {
                    if (!item.isObject()) {
                        continue;
                    }
                    const auto &importObj = item.toObject();
                    std::string inferenceId;
                    {
                        auto idIt = importObj.find("id");
                        if (idIt != importObj.end() && idIt->second.isString()) {
                            inferenceId = idIt->second.toString();
                        }
                        if (inferenceId.empty()) {
                            auto infIt = importObj.find("inferenceId");
                            if (infIt != importObj.end() && infIt->second.isString()) {
                                inferenceId = infIt->second.toString();
                            }
                        }
                    }
                    if (inferenceId.empty()) {
                        // Skip imports without an inferenceId reference.
                        continue;
                    }
                    SingerImport singerImport;
                    {
                        auto optIt = importObj.find("options");
                        if (optIt != importObj.end()) {
                            singerImport._manifestOptions = optIt->second;
                        }
                        // Cross-package declaration: "package" specifies the
                        // target package id; "version" specifies the required
                        // version ("*" or empty = any version).
                        auto pkgIt = importObj.find("package");
                        if (pkgIt != importObj.end() && pkgIt->second.isString()) {
                            singerImport._declaredPackage = pkgIt->second.toString();
                        }
                        auto verIt = importObj.find("version");
                        if (verIt != importObj.end() && verIt->second.isString()) {
                            singerImport._declaredVersion = verIt->second.toString();
                        }
                    }
                    impl.importInferenceIds.push_back(std::move(inferenceId));
                    impl.imports.push_back(std::move(singerImport));
                }
            }
        }

        impl.m_path = basePath;
        return spec;
    }

    core::Expected<void> SingerCategory::loadSpec(core::ModuleSpec *spec,
                                                   core::ModuleSpec::State state) {
        // Delegate base state management to loadSpecBase (updates modules list, state).
        auto baseResult = loadSpecBase(spec, state);
        if (!baseResult) {
            return baseResult;
        }

        auto *singerSpec = spec->as<SingerSpec>();
        auto &impl = *static_cast<SingerSpec::Impl *>(singerSpec->_impl.get());

        switch (state) {
            case core::ModuleSpec::Initialized: {
                // Resolve InferenceSpec pointers for each import by matching
                // importInferenceIds against registered InferenceSpec::id().
                auto *rt = runtime();
                if (!rt) {
                    break;
                }
                auto *infCat = rt->moduleCategory("inference");
                if (!infCat) {
                    break;
                }
                const auto inferenceSpecs = infCat->specs();
                for (size_t i = 0; i < impl.imports.size() && i < impl.importInferenceIds.size(); ++i) {
                    const auto &inferenceId = impl.importInferenceIds[i];
                    if (inferenceId.empty()) {
                        continue;
                    }
                    const auto &import = impl.imports[i];
                    InferenceSpec *found = nullptr;
                    for (auto *modSpec : inferenceSpecs) {
                        if (modSpec->id() != inferenceId) {
                            continue;
                        }
                        auto *infSpec = modSpec->as<InferenceSpec>();
                        if (import._declaredPackage.empty()) {
                            // Same-package strict matching: resolve only within
                            // the singer's own package identity (packageId +
                            // packageVersion). Multiple packages can reuse
                            // inference ids like "pitch" safely.
                            if (infSpec->packageId() == impl.m_packageId &&
                                infSpec->packageVersion() == impl.m_packageVersion) {
                                found = infSpec;
                                break;
                            }
                        } else {
                            // Cross-package matching (ARCH-06): resolve by
                            // declared package id. Version supports wildcard
                            // ("*" or empty = any) or exact match.
                            if (infSpec->packageId() != import._declaredPackage) {
                                continue;
                            }
                            if (import._declaredVersion.empty() ||
                                import._declaredVersion == "*") {
                                found = infSpec;
                                break;
                            }
                            auto declaredVer =
                                stdc::VersionNumber::fromString(import._declaredVersion).value_or(stdc::VersionNumber());
                            if (infSpec->packageVersion() == declaredVer) {
                                found = infSpec;
                                break;
                            }
                        }
                    }
                    if (found) {
                        impl.imports[i]._inference = found;
                    } else {
                        std::string pkgDesc;
                        if (import._declaredPackage.empty()) {
                            pkgDesc = impl.m_packageId + "[" +
                                      impl.m_packageVersion.toString() + "]";
                        } else {
                            pkgDesc = import._declaredPackage + "[" +
                                      (import._declaredVersion.empty() ||
                                               import._declaredVersion == "*"
                                           ? "*"
                                           : import._declaredVersion) +
                                      "]";
                        }
                        // ROBUST-05: loadSpecBase(Initialized) 已将 spec 加入
                        // modules 列表，若此处直接返回错误，调用方（Runtime::
                        // loadPackage）会认为 initResult 失败而不再调用
                        // loadSpec(Deleted)，导致 spec 仍在列表中但已被 unique_ptr
                        // 释放，~Impl() 将再次 delete 同一指针 → 双重释放 →
                        // 堆 corruption (tests 635/636)。修复：返回错误前先回滚
                        // loadSpecBase 的副作用，维持 "loadSpec 返回错误则 spec
                        // 不在 modules 列表" 的契约。
                        (void) loadSpecBase(spec, core::ModuleSpec::Deleted);
                        return core::Error{
                            core::Error::InvalidArgument,
                            "singer '" + impl.m_id + "' import '" + inferenceId +
                                "' not found in package " + pkgDesc};
                    }
                }
                break;
            }

            case core::ModuleSpec::Ready: {
                // Create InferenceImportOptions for each resolved import. The
                // InferenceSpec's interpreter must already be initialized
                // (InferenceCategory::loadSpec(Initialized/Ready) runs first
                // because Runtime::loadPackage loads inferences before singers).
                for (auto &import : impl.imports) {
                    if (!import._inference) {
                        continue;
                    }
                    auto exp = import._inference->createImportOptions(import._manifestOptions);
                    if (exp) {
                        import._options = std::move(*exp);
                    }
                    // Errors are non-fatal: the import is still registered
                    // (with null _options) so callers can detect the gap.
                }
                break;
            }

            case core::ModuleSpec::Finished:
            case core::ModuleSpec::Deleted:
                // Release resolved references and parsed options.
                for (auto &import : impl.imports) {
                    import._inference = nullptr;
                    import._options.reset();
                }
                impl.singerConfiguration.reset();
                break;

            default:
                break;
        }
        return {};
    }

    // ============================================================================
    // Force-export SingerProvider & SingerProviderPlugin symbols
    // ============================================================================

    SingerProvider::SingerProvider(const SingerSpec *spec) : _spec(spec) {}
    SingerProvider::~SingerProvider() = default;
    SingerProviderPlugin::~SingerProviderPlugin() = default;

    // ============================================================================
    // Registrar
    // ============================================================================

    static core::ModuleCategoryRegistrar<SingerCategory> g_singerRegistrar;

} // namespace srt::svs
