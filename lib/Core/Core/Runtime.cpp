#include "Runtime.h"
#include "Runtime_p.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <stdcorelib/path.h>
#include <stdcorelib/3rdparty/llvm/smallvector.h>

#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/JSON.h>

namespace fs = std::filesystem;

namespace srt::core {

    llvm::SmallVector<ModuleCategory *(*)(Runtime *)> Runtime::Impl::moduleCategoryFactories;

    Runtime::Impl::Impl(Runtime *decl) : _decl(decl) {
        // ARCH-03: compose services instead of inheriting PluginFactory/
        // ObjectPool. The PluginFactory provides the plugin discovery/load
        // ability (PluginService) and is owned by the ServiceRegistry.
        plugins = services.registerService<PluginFactory>(std::make_unique<PluginFactory>());
        objectPool = std::make_unique<ObjectPool>();
    }

    Runtime::Impl::~Impl() {
        for (const auto &[name, cate] : moduleCategories) {
            (void) name;
            delete cate;
        }
        moduleCategories.clear();
        moduleCateKeyMap.clear();
    }

    Runtime::Runtime() : _impl(new Impl(this)) {
        for (const auto &fac : Impl::moduleCategoryFactories) {
            auto *cate = fac(this);
            if (!cate) {
                continue;
            }
            _impl->moduleCategories.emplace(cate->name(), cate);
            _impl->moduleCateKeyMap.emplace(cate->key(), cate);
        }
    }

    Runtime::~Runtime() = default;

    // --- Service registry (ARCH-03) ---

    ServiceRegistry &Runtime::services() {
        return _impl->services;
    }

    const ServiceRegistry &Runtime::services() const {
        return _impl->services;
    }

    // --- Stage 1: package scanning ---

    Expected<void> Runtime::scanPackages(const std::filesystem::path &rootDir) {
        if (_impl->initializing.load() || _impl->initialized.load()) {
            return Error{Diagnostic{
                ErrorCode::PackageScanAfterInitialize,
                Severity::Error,
                "cannot scan package sources after Runtime initialization has started",
            }};
        }

        if (rootDir.empty() || !fs::is_directory(rootDir)) {
            return Error{Diagnostic{
                ErrorCode::PackageRootInvalid,
                Severity::Error,
                "invalid package root directory",
                stdc::path::to_utf8(rootDir),
            }};
        }

        // Wrap filesystem operations in try-catch: fs::canonical and
        // fs::directory_iterator may throw filesystem_error on permission
        // denied, race conditions, or removed directories. Convert to Expected.
        std::filesystem::path canonical;
        try {
            canonical = fs::canonical(rootDir);

            // Stage 1: collect candidate package directories under the root.
            // Metadata parsing is consumed by the package service as the DS
            // Bank/Core boundary lands; this API is already immutable after
            // initialization.
            _impl->discoveredPackages.clear();
            for (const auto &entry : fs::directory_iterator(canonical)) {
                if (!entry.is_directory()) {
                    continue;
                }
                _impl->discoveredPackages.push_back(fs::canonical(entry.path()));
            }
        } catch (const std::exception &e) {
            return Error{
                Error::FileNotOpen,
                std::string("failed to scan package directory: ") + e.what(),
            };
        }

        _impl->scanRoot = canonical;
        _impl->scanned = true;
        return Expected<void>();
    }

    Expected<void> Runtime::loadPackage(const std::filesystem::path &path) {
        // Load DiffSinger voice bank packages by parsing desc.json and
        // delegating to InferenceCategory/SingerCategory for spec creation.
        //
        // The desc.json "contributes" section lists inference and singer config
        // file paths; each config file is parsed by the corresponding category's
        // parseSpec, then loadSpec transitions the spec through Initialized ->
        // Ready. Inferences are loaded first so that singer
        // loadSpec(Initialized) can resolve InferenceSpec pointers by inferenceId.

        // 1. Check path/desc.json exists (ROBUST-02: filesystem boundary).
        const auto descPath = path / "desc.json";
        std::error_code ec;
        if (!fs::exists(descPath, ec)) {
            return Error{
                Error::FileNotFound,
                "package manifest not found: " + stdc::path::to_utf8(descPath),
            };
        }

        // 2. Read desc.json (ROBUST-02: filesystem I/O boundary).
        std::string text;
        try {
            std::ifstream ifs(descPath);
            if (!ifs.is_open()) {
                return Error{
                    Error::FileNotOpen,
                    "failed to open package manifest: " + stdc::path::to_utf8(descPath),
                };
            }
            std::stringstream ss;
            ss << ifs.rdbuf();
            text = ss.str();
        } catch (const std::exception &e) {
            return Error{
                Error::FileNotOpen,
                std::string("failed to read manifest: ") + e.what(),
            };
        }

        // 3. Parse JSON (ROBUST-02: JSON parse boundary).
        std::string parseErr;
        auto root = JsonValue::fromJson(text, true, &parseErr);
        if (!parseErr.empty()) {
            return Error{
                Error::InvalidFormat,
                "invalid package manifest format: " + parseErr,
            };
        }
        if (!root.isObject()) {
            return Error{
                Error::InvalidFormat,
                "package manifest must be a JSON object",
            };
        }
        const auto &obj = root.toObject();

        // 4. Extract package identity from desc.json id and version.
        std::string pkgId;
        stdc::VersionNumber pkgVersion;
        {
            auto it = obj.find("id");
            if (it != obj.end() && it->second.isString()) {
                pkgId = it->second.toString();
            }
            it = obj.find("version");
            if (it != obj.end() && it->second.isString()) {
                pkgVersion = stdc::VersionNumber::fromString(it->second.toString());
            }
        }
        if (pkgId.empty()) {
            return Error{Error::InvalidFormat, "package manifest id must be a non-empty string"};
        }
        if (pkgVersion.isEmpty()) {
            return Error{Error::InvalidFormat, "package manifest version must be a non-empty version string"};
        }

        // 5. Extract contributes.inferences and contributes.singers (file path arrays).
        std::vector<fs::path> inferenceRefs;
        std::vector<fs::path> singerRefs;
        {
            auto it = obj.find("contributes");
            if (it != obj.end() && it->second.isObject()) {
                const auto &contrib = it->second.toObject();
                std::string refError;
                auto parseRefs = [&path, &refError](const JsonObject &contribObj,
                                                     const char *key) -> std::vector<fs::path> {
                    std::vector<fs::path> refs;
                    auto refIt = contribObj.find(key);
                    if (refIt == contribObj.end() || !refIt->second.isArray()) {
                        return refs;
                    }
                    for (const auto &item : refIt->second.toArray()) {
                        if (!item.isString()) {
                            refError = std::string("contributes.") + key +
                                       " array contains non-string item";
                            return {};
                        }
                        fs::path p(item.toString());
                        if (!p.is_absolute()) {
                            p = path / p;
                        }
                        refs.push_back(p.lexically_normal());
                    }
                    return refs;
                };
                inferenceRefs = parseRefs(contrib, "inferences");
                if (!refError.empty()) {
                    return Error{Error::InvalidFormat, refError};
                }
                singerRefs = parseRefs(contrib, "singers");
                if (!refError.empty()) {
                    return Error{Error::InvalidFormat, refError};
                }
            }
        }

        // 6. Load inference specs FIRST (singers depend on them).
        auto *infCat = moduleCategory("inference");
        if (!infCat) {
            return Error{
                Error::FeatureNotSupported,
                "inference module category is not registered",
            };
        }
        for (const auto &ref : inferenceRefs) {
            if (!fs::exists(ref, ec)) {
                return Error{
                    Error::FileNotFound,
                    "inference config referenced in desc.json not found: " + stdc::path::to_utf8(ref),
                };
            }
            // Read config file.
            std::string configText;
            try {
                std::ifstream ifs(ref);
                if (!ifs.is_open()) {
                    return Error{
                        Error::FileNotOpen,
                        "failed to open inference config: " + stdc::path::to_utf8(ref),
                    };
                }
                std::stringstream ss;
                ss << ifs.rdbuf();
                configText = ss.str();
            } catch (const std::exception &e) {
                return Error{
                    Error::FileNotOpen,
                    "failed to read inference config: " + stdc::path::to_utf8(ref) +
                        ": " + e.what(),
                };
            }

            std::string cfgErr;
            auto configJson = JsonValue::fromJson(configText, true, &cfgErr);
            if (!cfgErr.empty() || !configJson.isObject()) {
                return Error{
                    Error::InvalidFormat,
                    "invalid inference config at " + stdc::path::to_utf8(ref) + ": " + cfgErr,
                };
            }

            auto parseResult = infCat->parseSpec(ref.parent_path(), configJson);
            if (!parseResult) {
                return Error{parseResult.error()};
            }
            auto *spec = parseResult.value();
            spec->_impl->packageId = pkgId;
            spec->_impl->packageVersion = pkgVersion;

            auto initResult = infCat->loadSpec(spec, ModuleSpec::Initialized);
            if (!initResult) {
                return Error{initResult.error()};
            }
            auto readyResult = infCat->loadSpec(spec, ModuleSpec::Ready);
            if (!readyResult) {
                return Error{readyResult.error()};
            }
        }

        // 7. Load singer specs.
        auto *singerCat = moduleCategory("singer");
        if (!singerCat) {
            return Error{
                Error::FeatureNotSupported,
                "singer module category is not registered",
            };
        }
        for (const auto &ref : singerRefs) {
            if (!fs::exists(ref, ec)) {
                return Error{
                    Error::FileNotFound,
                    "singer config referenced in desc.json not found: " + stdc::path::to_utf8(ref),
                };
            }
            std::string configText;
            try {
                std::ifstream ifs(ref);
                if (!ifs.is_open()) {
                    return Error{
                        Error::FileNotOpen,
                        "failed to open singer config: " + stdc::path::to_utf8(ref),
                    };
                }
                std::stringstream ss;
                ss << ifs.rdbuf();
                configText = ss.str();
            } catch (const std::exception &e) {
                return Error{
                    Error::FileNotOpen,
                    "failed to read singer config: " + stdc::path::to_utf8(ref) +
                        ": " + e.what(),
                };
            }

            std::string cfgErr;
            auto configJson = JsonValue::fromJson(configText, true, &cfgErr);
            if (!cfgErr.empty() || !configJson.isObject()) {
                return Error{
                    Error::InvalidFormat,
                    "invalid singer config at " + stdc::path::to_utf8(ref) + ": " + cfgErr,
                };
            }

            auto parseResult = singerCat->parseSpec(ref.parent_path(), configJson);
            if (!parseResult) {
                return Error{parseResult.error()};
            }
            auto *spec = parseResult.value();
            spec->_impl->packageId = pkgId;
            spec->_impl->packageVersion = pkgVersion;

            auto initResult = singerCat->loadSpec(spec, ModuleSpec::Initialized);
            if (!initResult) {
                return Error{initResult.error()};
            }
            auto readyResult = singerCat->loadSpec(spec, ModuleSpec::Ready);
            if (!readyResult) {
                return Error{readyResult.error()};
            }
        }

        return Expected<void>();
    }

    // --- Stage 2: initialization ---

    Expected<void> Runtime::initialize() {
        // Stage 2 is one-shot hard-idempotent: once it has started or completed,
        // reject any re-entry. Mirrors the LangCore Manager::initialize()
        // contract.
        bool expected = false;
        if (!_impl->initializing.compare_exchange_strong(expected, true)) {
            return Error{
                Error::InvalidArgument,
                "Runtime is already initializing or initialized",
            };
        }

        if (_impl->initialized.load()) {
            return Error{
                Error::InvalidArgument,
                "Runtime is already initialized",
            };
        }

        // Stage 2 sub-steps. These depend on modules not yet migrated into the
        // v4 graph; each lands in its respective phase. Until then the body is
        // a structured no-op that succeeds:
        //   2.1 register ONNX driver (srt.driver.onnx)              — Phase 9
        //   2.2 build context index from LanguageConfig             — Phase 6
        //   2.3 bind models to drivers (ds::infer)                  — Phase 7
        //   2.4 mark Ready, expose context state                     — Phase 8
        // The atomic guards above already enforce the one-shot contract; the
        // sub-step bodies are wired up as the dependencies land.

        // TODO(Phase 6/7/8/9): wire driver registration, context index and
        // model binding here. Each sub-step should report failure through
        // Result<T> (ROBUST-01) and leave initializing/initialized in a state
        // that prevents silent retry (infrastructure failure blocks).

        _impl->initialized.store(true);
        return Expected<void>();
    }

    bool Runtime::isInitialized() const {
        return _impl->initialized.load();
    }

    std::vector<std::filesystem::path> Runtime::discoveredPackages() const {
        return _impl->discoveredPackages;
    }

    ModuleCategory *Runtime::moduleCategory(const std::string_view &name) const {
        if (auto it = _impl->moduleCategories.find(name); it != _impl->moduleCategories.end()) {
            return it->second;
        }
        return nullptr;
    }

    void Runtime::registerModuleCategoryFactory(ModuleCategory *(*fac)(Runtime *)) {
        Impl::moduleCategoryFactories.push_back(fac);
    }

}
