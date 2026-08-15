#include "Runtime.h"

#include <stdcorelib/path.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/Support/Logging.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>

#include "Runtime_p.h"

// Module_p.h defines ModuleSpec::Impl (with packageId/packageVersion fields)
// which Runtime::loadPackage writes to inject package identity. The header is
// private to the srt-core target (lib/Core/Module/) and is reachable via the
// target's PRIVATE include directories.
#include "Module_p.h"

namespace fs = std::filesystem;

namespace srt::core {

    static srt::LogCategory RuntimeLog("core.runtime");

    std::vector<ModuleCategory *(*)(Runtime *)> Runtime::Impl::moduleCategoryFactories;

    Runtime::Impl::Impl(Runtime *q) : m_q(q) {
        // ARCH-03: compose services instead of inheriting PluginFactory/
        // ObjectPool. The PluginFactory provides the plugin discovery/load
        // ability (PluginService) and is owned by the ServiceRegistry.
        m_plugins    = m_services.registerService<PluginFactory>(std::make_unique<PluginFactory>());
        m_objectPool = std::make_unique<ObjectPool>();
    }

    Runtime::Impl::~Impl() {
        // Run destruction callbacks in LIFO order BEFORE deleting module
        // categories or unloading plugin DLLs (the PluginFactory is owned by
        // m_services, which is destroyed after this destructor body runs as
        // part of member destruction). This lets subsystems release shared_ptrs
        // to plugin-DLL-resident objects while the DLLs are still loaded.
        for (auto it = m_destructionCallbacks.rbegin(); it != m_destructionCallbacks.rend(); ++it) {
            if (*it) {
                (*it)();
            }
        }
        m_destructionCallbacks.clear();

        // Call the global shutdown hook (set by static-singleton subsystems
        // like srt::g2p::PackageManager). This is the key mechanism for
        // subsystems that cannot access a Runtime instance to call
        // addDestructionCallback(). The hook is cleared after execution so
        // that if multiple Runtime instances exist, only the first one's
        // destruction runs the hook.
        auto &hook = globalShutdownHook();
        if (hook) {
            hook();
            hook = nullptr;
        }

        for (const auto &[name, cate] : m_moduleCategories) {
            (void)name;
            delete cate;
        }
        m_moduleCategories.clear();
        m_moduleCateKeyMap.clear();
    }

    Runtime::Runtime() : _impl(new Impl(this)) {
        for (const auto &fac : Impl::moduleCategoryFactories) {
            auto *cate = fac(this);
            if (!cate) {
                continue;
            }
            _impl->m_moduleCategories.emplace(cate->name(), cate);
            _impl->m_moduleCateKeyMap.emplace(cate->key(), cate);
        }
    }

    Runtime::~Runtime() = default;

    void Runtime::addDestructionCallback(std::function<void()> callback) {
        _impl->m_destructionCallbacks.push_back(std::move(callback));
    }

    std::function<void()> &Runtime::Impl::globalShutdownHook() {
        // Meyers singleton: initialized on first call, destroyed at process exit.
        // This avoids the static initialization order fiasco — the function-local
        // static is guaranteed to be initialized before any caller uses it.
        static std::function<void()> hook;
        return hook;
    }

    void Runtime::setGlobalShutdownHook(std::function<void()> hook) {
        Runtime::Impl::globalShutdownHook() = std::move(hook);
    }

    // --- Service registry (ARCH-03) ---

    ServiceRegistry &Runtime::services() {
        return _impl->m_services;
    }

    const ServiceRegistry &Runtime::services() const {
        return _impl->m_services;
    }

    // --- Stage 1: package scanning ---

    Expected<void> Runtime::scanPackages(const std::filesystem::path &rootDir) {
        if (_impl->m_initializing.load() || _impl->m_initialized.load()) {
            return Error{
                Diagnostic{
                           ErrorCode::PackageScanAfterInitialize,
                           Severity::Error,
                           "cannot scan package sources after Runtime initialization has started", }
            };
        }

        if (rootDir.empty() || !fs::is_directory(rootDir)) {
            return Error{
                Diagnostic{
                           ErrorCode::PackageRootInvalid,
                           Severity::Error,
                           "invalid package root directory", stdc::path::to_utf8(rootDir),
                           }
            };
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
            _impl->m_discoveredPackages.clear();
            for (const auto &entry : fs::directory_iterator(canonical)) {
                if (!entry.is_directory()) {
                    continue;
                }
                _impl->m_discoveredPackages.push_back(fs::canonical(entry.path()));
            }
        } catch (const std::exception &e) {
            return Error{
                Error::FileNotOpen,
                std::string("failed to scan package directory: ") + e.what(),
            };
        }

        _impl->m_scanRoot = canonical;
        _impl->m_scanned  = true;
        return Expected<void>();
    }

    Expected<void> Runtime::loadPackage(const std::filesystem::path &path) {
        // CODING-04: serialize loadPackage to prevent TOCTOU races where two
        // concurrent callers both pass duplicate detection and both commit specs.
        // su_mtx is declared in Runtime::Impl (Runtime_p.h) and protects category
        // / contribute operations across packages. Public API signature is
        // unchanged (D-11).
        std::unique_lock<std::shared_mutex> suLock(_impl->m_su_mtx);

        // Load DiffSinger voice bank packages by parsing desc.json and
        // delegating to InferenceCategory/SingerCategory for spec creation.
        //
        // The desc.json "contributes" section lists inference and singer config
        // file paths; each config file is parsed by the corresponding category's
        // parseSpec, then loadSpec transitions the spec through Initialized ->
        // Ready. Inferences are loaded first so that singer
        // loadSpec(Initialized) can resolve InferenceSpec pointers by inferenceId.

        // 1. Check path/desc.json exists (ROBUST-02: filesystem boundary).
        const auto      descPath = path / "desc.json";
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
        auto        root = JsonValue::fromJson(text, true, &parseErr);
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
        std::string         pkgId;
        stdc::VersionNumber pkgVersion;
        {
            auto it = obj.find("id");
            if (it != obj.end() && it->second.isString()) {
                pkgId = it->second.toString();
            }
            it = obj.find("version");
            if (it != obj.end() && it->second.isString()) {
                pkgVersion = stdc::VersionNumber::fromString(it->second.toString()).value_or(stdc::VersionNumber());
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
                auto        parseRefs = [&path, &refError](const JsonObject &contribObj,
                                                    const char       *key) -> std::vector<fs::path> {
                    std::vector<fs::path> refs;
                    auto                  refIt = contribObj.find(key);
                    if (refIt == contribObj.end() || !refIt->second.isArray()) {
                        return refs;
                    }
                    for (const auto &item : refIt->second.toArray()) {
                        if (!item.isString()) {
                            refError = std::string("contributes.") + key + " array contains non-string item";
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

        // Track specs committed during this loadPackage call. On failure, they
        // are rolled back via loadSpec(Deleted) + delete to avoid leaving
        // partial state in the category's modules list (which would cause
        // PackageDuplicate on the next retry). Per ROBUST-05: errors must not
        // silently corrupt state. The pending spec (parseSpec'd but not yet
        // committed) is held by a unique_ptr so it is freed on early return.
        struct CommittedSpec {
            ModuleCategory *cat;
            ModuleSpec     *spec;
        };
        std::vector<CommittedSpec> committed;
        auto                       rollbackCommitted = [&committed]() {
            // Roll back in reverse order (singers were appended after inferences,
            // so they roll back first — matching the dependency direction).
            for (auto it = committed.rbegin(); it != committed.rend(); ++it) {
                // ROBUST-05: do not silently swallow rollback errors. Log a
                // warning and still delete the spec below to avoid leaks.
                auto delResult = it->cat->loadSpec(it->spec, ModuleSpec::Deleted);
                if (!delResult) {
                    RuntimeLog.srtWarning("loadPackage rollback: loadSpec(Deleted) failed for spec '%1': %2",
                                                                it->spec->id(), delResult.errorString());
                }
                delete it->spec;
            }
            committed.clear();
        };

        for (const auto &ref : inferenceRefs) {
            if (!fs::exists(ref, ec)) {
                rollbackCommitted();
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
                    rollbackCommitted();
                    return Error{
                        Error::FileNotOpen,
                        "failed to open inference config: " + stdc::path::to_utf8(ref),
                    };
                }
                std::stringstream ss;
                ss << ifs.rdbuf();
                configText = ss.str();
            } catch (const std::exception &e) {
                rollbackCommitted();
                return Error{
                    Error::FileNotOpen,
                    "failed to read inference config: " + stdc::path::to_utf8(ref) + ": " + e.what(),
                };
            }

            std::string cfgErr;
            auto        configJson = JsonValue::fromJson(configText, true, &cfgErr);
            if (!cfgErr.empty() || !configJson.isObject()) {
                rollbackCommitted();
                return Error{
                    Error::InvalidFormat,
                    "invalid inference config at " + stdc::path::to_utf8(ref) + ": " + cfgErr,
                };
            }

            auto parseResult = infCat->parseSpec(ref.parent_path(), configJson);
            if (!parseResult) {
                rollbackCommitted();
                return std::move(parseResult.takeError()
                                     .withTrace(std::source_location::current(), "Runtime::loadPackage")
                                     .withContext({}, {}, pkgId));
            }
            // unique_ptr frees spec on early return (e.g. duplicate detection,
            // loadSpec failure) — plug the previous leak where parseSpec'd specs
            // were not deleted on the duplicate-detection error path.
            std::unique_ptr<ModuleSpec> spec(parseResult.value());
            spec->_impl->m_packageId      = pkgId;
            spec->_impl->m_packageVersion = pkgVersion;

            // Duplicate detection: reject a spec whose (id, packageId,
            // packageVersion) strictly matches an already-loaded inference
            // spec. Different versions of the same packageId are allowed
            // (multi-version isolation); only strict id+version duplicates are
            // rejected. Model file paths are intentionally NOT compared:
            // different packages may legitimately share the same .onnx files
            // on disk.
            for (auto *existing : infCat->specs()) {
                if (existing->id() == spec->id() && existing->packageId() == pkgId &&
                    existing->packageVersion() == pkgVersion) {
                    rollbackCommitted();
                    return Error::packageError(ErrorCode::PackageDuplicate,
                                               "duplicate inference spec already loaded: id='" + spec->id() +
                                                   "' in package " + pkgId + "[" + pkgVersion.toString() + "]",
                                               pkgId);
                }
            }

            auto initResult = infCat->loadSpec(spec.get(), ModuleSpec::Initialized);
            if (!initResult) {
                // loadSpecBase only adds to modules on success, so spec is NOT
                // in the list here — unique_ptr will free it.
                rollbackCommitted();
                return std::move(initResult.takeError()
                                     .withTrace(std::source_location::current(), "Runtime::loadPackage")
                                     .withContext({}, {}, pkgId));
            }
            auto readyResult = infCat->loadSpec(spec.get(), ModuleSpec::Ready);
            if (!readyResult) {
                // Initialized succeeded, so spec IS in the modules list —
                // remove it before unique_ptr frees the memory.
                auto infDelResult = infCat->loadSpec(spec.get(), ModuleSpec::Deleted);
                if (!infDelResult) {
                    // ROBUST-05: log the rollback failure instead of swallowing.
                    RuntimeLog.srtWarning(
                        "loadPackage: failed to roll back inference spec '%1' after Ready failure: %2", spec->id(),
                        infDelResult.errorString());
                }
                rollbackCommitted();
                return std::move(readyResult.takeError()
                                     .withTrace(std::source_location::current(), "Runtime::loadPackage")
                                     .withContext({}, {}, pkgId));
            }
            // Success: commit (release ownership — category now holds the raw
            // pointer in its modules list, matching the existing ownership model).
            committed.push_back({infCat, spec.get()});
            (void)spec.release();
        }

        // 7. Load singer specs.
        auto *singerCat = moduleCategory("singer");
        if (!singerCat) {
            // Roll back the inferences committed above — singer category is
            // required for a coherent package load.
            rollbackCommitted();
            return Error{
                Error::FeatureNotSupported,
                "singer module category is not registered",
            };
        }
        for (const auto &ref : singerRefs) {
            if (!fs::exists(ref, ec)) {
                rollbackCommitted();
                return Error{
                    Error::FileNotFound,
                    "singer config referenced in desc.json not found: " + stdc::path::to_utf8(ref),
                };
            }
            std::string configText;
            try {
                std::ifstream ifs(ref);
                if (!ifs.is_open()) {
                    rollbackCommitted();
                    return Error{
                        Error::FileNotOpen,
                        "failed to open singer config: " + stdc::path::to_utf8(ref),
                    };
                }
                std::stringstream ss;
                ss << ifs.rdbuf();
                configText = ss.str();
            } catch (const std::exception &e) {
                rollbackCommitted();
                return Error{
                    Error::FileNotOpen,
                    "failed to read singer config: " + stdc::path::to_utf8(ref) + ": " + e.what(),
                };
            }

            std::string cfgErr;
            auto        configJson = JsonValue::fromJson(configText, true, &cfgErr);
            if (!cfgErr.empty() || !configJson.isObject()) {
                rollbackCommitted();
                return Error{
                    Error::InvalidFormat,
                    "invalid singer config at " + stdc::path::to_utf8(ref) + ": " + cfgErr,
                };
            }

            auto parseResult = singerCat->parseSpec(ref.parent_path(), configJson);
            if (!parseResult) {
                rollbackCommitted();
                return std::move(parseResult.takeError()
                                     .withTrace(std::source_location::current(), "Runtime::loadPackage")
                                     .withContext({}, {}, pkgId));
            }
            std::unique_ptr<ModuleSpec> spec(parseResult.value());
            spec->_impl->m_packageId      = pkgId;
            spec->_impl->m_packageVersion = pkgVersion;

            // Duplicate detection: reject a singer whose (singerId, packageId,
            // packageVersion) strictly matches an already-loaded singer. Same
            // semantics as the inference check above — multi-version isolation
            // preserved, model file paths not compared.
            for (auto *existing : singerCat->specs()) {
                if (existing->id() == spec->id() && existing->packageId() == pkgId &&
                    existing->packageVersion() == pkgVersion) {
                    rollbackCommitted();
                    return Error::packageError(ErrorCode::PackageDuplicate,
                                               "duplicate singer spec already loaded: id='" + spec->id() +
                                                   "' in package " + pkgId + "[" + pkgVersion.toString() + "]",
                                               pkgId);
                }
            }

            auto initResult = singerCat->loadSpec(spec.get(), ModuleSpec::Initialized);
            if (!initResult) {
                rollbackCommitted();
                return std::move(initResult.takeError()
                                     .withTrace(std::source_location::current(), "Runtime::loadPackage")
                                     .withContext({}, {}, pkgId));
            }
            auto readyResult = singerCat->loadSpec(spec.get(), ModuleSpec::Ready);
            if (!readyResult) {
                auto singerDelResult = singerCat->loadSpec(spec.get(), ModuleSpec::Deleted);
                if (!singerDelResult) {
                    // ROBUST-05: log the rollback failure instead of swallowing.
                    RuntimeLog.srtWarning("loadPackage: failed to roll back singer spec '%1' after Ready failure: %2",
                                          spec->id(), singerDelResult.errorString());
                }
                rollbackCommitted();
                return std::move(readyResult.takeError()
                                     .withTrace(std::source_location::current(), "Runtime::loadPackage")
                                     .withContext({}, {}, pkgId));
            }
            committed.push_back({singerCat, spec.get()});
            (void)spec.release();
        }

        // Record the loaded package so unloadPackage(path) can resolve the
        // pkgId without re-parsing desc.json (which may have been modified
        // on disk between load and unload). Use canonical path to tolerate
        // trailing-slash / relative-input differences at the call site.
        std::error_code canonEc;
        auto            canonicalPath = fs::canonical(path, canonEc);
        if (canonEc) {
            // Canonicalization may fail if the directory was removed between
            // the fs::exists checks above and here. Fall back to lexically
            // normal form so we still record something usable; unloadPackage
            // will perform the same normalization on its input.
            canonicalPath = path.lexically_normal();
        }
        _impl->m_loadedPackages.push_back({canonicalPath, pkgId});

        return Expected<void>();
    }

    Expected<void> Runtime::unloadPackage(const std::filesystem::path &path) {
        // CODING-04: serialize against concurrent loadPackage / unloadPackage
        // to prevent TOCTOU on loadedPackages and the category modules lists.
        std::unique_lock<std::shared_mutex> suLock(_impl->m_su_mtx);

        // Resolve the canonical path the same way loadPackage does, so path
        // identity holds across trailing-slash / relative-input differences.
        std::error_code canonEc;
        auto            canonicalPath = fs::canonical(path, canonEc);
        if (canonEc) {
            canonicalPath = path.lexically_normal();
        }

        // 1. Look up the loaded package by canonical path. If not found,
        // return RuntimePackageNotLoaded so callers can distinguish "not
        // loaded by Runtime" from "filesystem missing".
        std::string pkgId;
        auto        loadedIt =
            std::find_if(_impl->m_loadedPackages.begin(), _impl->m_loadedPackages.end(),
                         [&](const Runtime::Impl::LoadedPackage &p) { return p.canonicalPath == canonicalPath; });
        if (loadedIt == _impl->m_loadedPackages.end()) {
            return Error::packageError(ErrorCode::RuntimePackageNotLoaded,
                                       "package not loaded: " + stdc::path::to_utf8(canonicalPath));
        }
        pkgId = loadedIt->packageId;

        // 2. Collect specs to unload. Inferences are loaded first by
        // loadPackage, singers second; unload in reverse order (singers
        // first) so that singer specs — which may hold InferenceSpec pointers
        // resolved at Initialized time — are torn down before the inference
        // specs they reference. This mirrors the rollbackCommitted lambda in
        // loadPackage.
        struct SpecToRemove {
            ModuleCategory *cat;
            ModuleSpec     *spec;
        };
        std::vector<SpecToRemove> toRemove;

        auto collectFrom = [&](ModuleCategory *cat) {
            if (!cat) {
                return;
            }
            for (auto *spec : cat->specs()) {
                if (spec->packageId() == pkgId) {
                    toRemove.push_back({cat, spec});
                }
            }
        };

        // Singer first (appended last by loadPackage, removed first here).
        auto *singerCat = moduleCategory("singer");
        collectFrom(singerCat);
        auto *infCat = moduleCategory("inference");
        collectFrom(infCat);

        if (toRemove.empty()) {
            // No specs reference this package — still remove the loaded-
            // package record so the caller can re-load later.
            _impl->m_loadedPackages.erase(loadedIt);
            return Expected<void>();
        }

        // 3. Transition each spec to Deleted, then free its memory. This
        // matches the rollback path in loadPackage: loadSpec(Deleted) removes
        // the spec from the category's modules list (and lets derived
        // categories release resolved pointers — see
        // SingerCategory::loadSpec(Deleted) clearing _inference). After
        // loadSpec(Deleted) succeeds, the spec is no longer reachable from
        // the category, so delete is safe.
        srt::core::Error firstError;
        bool             hadError = false;
        for (const auto &entry : toRemove) {
            auto delResult = entry.cat->loadSpec(entry.spec, ModuleSpec::Deleted);
            if (!delResult) {
                // ROBUST-05: log and continue rather than swallowing. The
                // spec may still be in the modules list in this case; we do
                // NOT delete it to avoid double-free when ~ModuleCategory
                // runs.
                if (!hadError) {
                    firstError = std::move(delResult.takeError()
                                               .withTrace(std::source_location::current(), "Runtime::unloadPackage")
                                               .withContext({}, {}, pkgId));
                    hadError   = true;
                }
                RuntimeLog.srtWarning("unloadPackage: loadSpec(Deleted) failed for spec '%1' in "
                                      "package %2: %3",
                                      entry.spec->id(), pkgId, delResult.errorString());
                continue;
            }
            delete entry.spec;
        }

        // 4. Remove from the loaded-package record regardless of partial
        // failure: the caller asked to unload, and we have done our best to
        // honor it. A subsequent loadPackage(path) will re-record the entry.
        _impl->m_loadedPackages.erase(loadedIt);

        if (hadError) {
            return firstError;
        }
        return Expected<void>();
    }

    // --- Stage 2: initialization ---

    Expected<void> Runtime::initialize() {
        // Stage 2 is one-shot hard-idempotent: once it has started or completed,
        // reject any re-entry. Mirrors the LangCore Manager::initialize()
        // contract.
        bool expected = false;
        if (!_impl->m_initializing.compare_exchange_strong(expected, true)) {
            return Error{
                Error::InvalidArgument,
                "Runtime is already initializing or initialized",
            };
        }

        if (_impl->m_initialized.load()) {
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

        _impl->m_initialized.store(true);
        return Expected<void>();
    }

    bool Runtime::isInitialized() const {
        return _impl->m_initialized.load();
    }

    std::vector<std::filesystem::path> Runtime::discoveredPackages() const {
        return _impl->m_discoveredPackages;
    }

    ModuleCategory *Runtime::moduleCategory(const std::string_view &name) const {
        if (auto it = _impl->m_moduleCategories.find(name); it != _impl->m_moduleCategories.end()) {
            return it->second;
        }
        return nullptr;
    }

    void Runtime::registerModuleCategoryFactory(ModuleCategory *(*fac)(Runtime *)) {
        Impl::moduleCategoryFactories.push_back(fac);
    }

} // namespace srt::core
