#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/G2P/Package/Package.h>
#include <synthrt/G2P/Support/ContextUtils.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/TaskPlugin.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <system_error>
#include <utility>

#include "../../Core/Module/Module_p.h"
#include "Core/PackageManager_p.h"
#include "Package/Package_p.h"

namespace fs = std::filesystem;

namespace srt::g2p {

    namespace {
        srt::core::LogCategory pkgMgrLog("g2p.packagemanager");

        /// Minimal ModuleSpec subclass for the "g2p" category.
        /// G2pCategory is declared as friend so parseSpec can access the
        /// protected _impl member inherited from ModuleSpec.
        class G2pCategory;

        class G2pSpec : public srt::core::ModuleSpec {
        public:
            G2pSpec() : srt::core::ModuleSpec("g2p") {
            }
            friend class G2pCategory;
        };

        /// Minimal ModuleSpec subclass for the "dict" category.
        class DictCategory;

        class DictSpec : public srt::core::ModuleSpec {
        public:
            DictSpec() : srt::core::ModuleSpec("dict") {
            }
            friend class DictCategory;
        };

        /// Concrete ModuleCategory for the "g2p" category.
        /// The G2P PackageManager is a standalone singleton (no Runtime),
        /// so categories are created directly with the manager pointer.
        class G2pCategory : public srt::core::ModuleCategory {
        public:
            explicit G2pCategory(void *mgr) : srt::core::ModuleCategory("g2p", mgr) {
            }

            std::string key() const override {
                return "g2p";
            }
            std::string category() const override {
                return "g2p";
            }

            srt::core::Expected<srt::core::ModuleSpec *> parseSpec(const std::filesystem::path &basePath,
                                                                   const srt::core::JsonValue  &config) const override;
        };

        /// Concrete ModuleCategory for the "dict" category.
        class DictCategory : public srt::core::ModuleCategory {
        public:
            explicit DictCategory(void *mgr) : srt::core::ModuleCategory("dict", mgr) {
            }

            std::string key() const override {
                return "dict";
            }
            std::string category() const override {
                return "dict";
            }

            srt::core::Expected<srt::core::ModuleSpec *> parseSpec(const std::filesystem::path &basePath,
                                                                   const srt::core::JsonValue  &config) const override;
        };

        /// Concrete ModuleCategory for the "driver" category.
        /// Hosts register G2P driver factories (e.g. g2pOnnxDriver) here via
        /// addObject() before Manager::initialize(). Drivers are registered
        /// programmatically, not parsed from package.json, so parseSpec uses
        /// the default NotImplemented behavior.
        class DriverCategory : public srt::core::ModuleCategory {
        public:
            explicit DriverCategory(void *mgr) : srt::core::ModuleCategory(kDriverCategory, mgr) {
            }

            std::string key() const override {
                return kDriverCategory;
            }
            std::string category() const override {
                return kDriverCategory;
            }
        };

        /// Load a config file referenced by "configuration" in a module entry.
        /// Returns the inner "configuration" sub-object and updates apiLevel.
        /// This is a free helper (no friend access needed - only uses public JSON API).
        struct LoadedConfig {
            srt::core::JsonObject configuration;
            int                   apiLevel = 1;
        };
        bool loadConfigFile(const std::filesystem::path &configPath, LoadedConfig &out) {
            auto expObj = PackageData::readDesc(configPath);
            if (!expObj)
                return false;
            auto configFile = expObj.take();
            auto cfgIt      = configFile.find("configuration");
            if (cfgIt != configFile.end() && cfgIt->second.isObject()) {
                out.configuration = cfgIt->second.toObject();
            }
            auto lvlIt = configFile.find("level");
            if (lvlIt != configFile.end() && lvlIt->second.isInt()) {
                out.apiLevel = static_cast<int>(lvlIt->second.toInt());
            }
            return true;
        }

        /// Common field extraction from a package.json module entry JSON object.
        /// Caller provides the resolved Impl reference (caller has friend access).
        struct ModuleEntryFields {
            std::string id;
            std::string className;
            int         level = 1;
            // configuration: either a file path (string) or an inline object
            std::string           configFileRel;
            srt::core::JsonObject inlineConfig;
            bool                  hasInlineConfig = false;
        };
        ModuleEntryFields extractModuleEntryFields(const srt::core::JsonObject &obj) {
            ModuleEntryFields f;
            {
                auto it = obj.find("moduleId");
                if (it != obj.end() && it->second.isString())
                    f.id = it->second.toString();
            }
            {
                auto it = obj.find("class");
                if (it != obj.end() && it->second.isString())
                    f.className = it->second.toString();
            }
            {
                auto it = obj.find("level");
                if (it != obj.end() && it->second.isInt())
                    f.level = static_cast<int>(it->second.toInt());
            }
            {
                auto it = obj.find("configuration");
                if (it != obj.end()) {
                    if (it->second.isString()) {
                        f.configFileRel = it->second.toString();
                    } else if (it->second.isObject()) {
                        f.inlineConfig    = it->second.toObject();
                        f.hasInlineConfig = true;
                    }
                }
            }
            return f;
        }

        srt::core::Expected<srt::core::ModuleSpec *> G2pCategory::parseSpec(const std::filesystem::path &basePath,
                                                                            const srt::core::JsonValue  &config) const {
            if (!config.isObject()) {
                return srt::core::Error{ErrorCode::G2pConfigError, "g2p module config must be a JSON object"};
            }
            auto *spec = new G2pSpec();
            // G2pCategory is a friend of G2pSpec, so it can access the
            // protected _impl inherited from ModuleSpec (protected access
            // through the derived type G2pSpec*).
            auto &impl = *spec->_impl;

            const auto &obj  = config.toObject();
            auto        f    = extractModuleEntryFields(obj);
            impl.m_id        = std::move(f.id);
            impl.m_className = std::move(f.className);
            impl.m_apiLevel  = f.level;

            // Default path is the package directory; when a config file is
            // referenced, relative paths inside it resolve relative to the
            // config file's parent directory (DiffSinger convention).
            std::filesystem::path resolvedPath = basePath;
            if (!f.configFileRel.empty()) {
                auto         configFilePath = basePath / f.configFileRel;
                LoadedConfig lc;
                if (loadConfigFile(configFilePath, lc)) {
                    impl.m_manifestConfiguration = std::move(lc.configuration);
                    impl.m_apiLevel              = lc.apiLevel;
                    resolvedPath                 = configFilePath.parent_path();
                }
            } else if (f.hasInlineConfig) {
                impl.m_manifestConfiguration = std::move(f.inlineConfig);
            }

            impl.m_path = resolvedPath;
            return spec;
        }

        srt::core::Expected<srt::core::ModuleSpec *> DictCategory::parseSpec(const std::filesystem::path &basePath,
                                                                             const srt::core::JsonValue &config) const {
            if (!config.isObject()) {
                return srt::core::Error{ErrorCode::G2pConfigError, "dict module config must be a JSON object"};
            }
            auto *spec = new DictSpec();
            auto &impl = *spec->_impl;

            const auto &obj  = config.toObject();
            auto        f    = extractModuleEntryFields(obj);
            impl.m_id        = std::move(f.id);
            impl.m_className = std::move(f.className);
            impl.m_apiLevel  = f.level;

            std::filesystem::path resolvedPath = basePath;
            if (!f.configFileRel.empty()) {
                auto         configFilePath = basePath / f.configFileRel;
                LoadedConfig lc;
                if (loadConfigFile(configFilePath, lc)) {
                    impl.m_manifestConfiguration = std::move(lc.configuration);
                    impl.m_apiLevel              = lc.apiLevel;
                    resolvedPath                 = configFilePath.parent_path();
                }
            } else if (f.hasInlineConfig) {
                impl.m_manifestConfiguration = std::move(f.inlineConfig);
            }

            impl.m_path = resolvedPath;
            return spec;
        }
    } // namespace

    PackageManager::Impl::Impl(PackageManager *q) : m_q(q) {
        m_categories[kG2pCategory]    = new G2pCategory(q);
        m_categories[kDictCategory]   = new DictCategory(q);
        m_categories[kDriverCategory] = new DriverCategory(q);
    }

    PackageManager::Impl::~Impl() {
        shutdown();
    }

    void PackageManager::Impl::shutdown() {
        // Idempotent: safe to call from both shutdown() and the destructor.
        // Also safe to call when m_categories is already empty (no-op).
        if (m_categories.empty() && m_tasks.empty()) {
            return;
        }

        // Destruction order (matters!):
        //
        // 1. m_tasks.clear() — release the NO<Task> references held by the
        //    task index. Tasks are also referenced by ObjectPool::m_objects
        //    (use_count >= 2), so this only drops the count, not the object.
        //
        // 2. cate->removeAllObjects() — release the NO<Task>/NO<NamedObject>
        //    shared_ptrs held by each category's ObjectPool. This is the last
        //    reference, so Task destructors run HERE. Tasks hold a non-owning
        //    `const ModuleSpec*` pointer (Task::Impl::m_spec); the pointed-to
        //    ModuleSpec is owned by PackageData and is still alive at this
        //    point (step 3 deletes it). Doing removeAllObjects AFTER
        //    closeAllLoadedPackages would leave m_spec dangling and cause
        //    use-after-free in Task subclass destructors, corrupting the heap
        //    and later crashing in shared_ptr::_Decref.
        //
        // 3. closeAllLoadedPackages() — delete PackageData (which deletes its
        //    ModuleSpec instances). Categories must still be alive because
        //    closePackage calls ModuleCategory::loadSpec(Deleted) to remove
        //    specs from m_modules.
        //
        // 4. delete cate — destroy the categories themselves.
        m_tasks.clear();
        for (const auto &[_, cate] : m_categories) {
            cate->removeAllObjects();
        }
        closeAllLoadedPackages();
        for (const auto &[_, cate] : m_categories) {
            delete cate;
        }
        m_categories.clear();
        m_cateKeyMap.clear();
    }

    void PackageManager::Impl::refreshPackageIndexes(const srt::core::ContextKey &ctxKey) {
        // Invalidate caches for all ctxKeys (not just the current one) to
        // avoid stale cache hits across ctxKeys, and clear the dirty flag
        // up-front so callers don't retry on early return or thrown errors.
        m_contextCachedIndexes.clear();
        m_contextModuleInfos.clear();
        m_packagePathsDirty = false;

        auto &cachedIndexes = m_contextCachedIndexes[ctxKey];
        cachedIndexes.clear();
        auto pathsIt = m_contextPackagePaths.find(ctxKey);
        if (pathsIt == m_contextPackagePaths.end())
            return;

        // ROBUST-02: Wrap filesystem operations in try-catch. fs::is_directory,
        // fs::directory_iterator, fs::exists and fs::canonical can throw
        // filesystem_error on permission denied, race conditions, or removed
        // directories. Without this guard, a transient filesystem error would
        // propagate as an uncaught exception and crash the host process. On
        // error, log a warning and continue with whatever was collected.
        try {
            for (const auto &path : pathsIt->second) {
                if (!fs::is_directory(path))
                    continue;
                for (const auto &entry : fs::directory_iterator(path)) {
                    if (!entry.is_directory())
                        continue;

                    auto pkgJson = entry.path() / "package.json";
                    if (!fs::exists(pkgJson))
                        continue;

                    auto expObj = PackageData::readDesc(pkgJson);
                    if (!expObj) {
                        // ROBUST-05: log skip reason instead of silently continuing.
                        pkgMgrLog.srtWarning(
                            "PackageManager: skipping package at '%1': failed to read package.json: %2",
                            stdc::path::to_utf8(pkgJson), expObj.errorString());
                        continue;
                    }
                    auto obj = expObj.take();

                    auto idIt = obj.find("packageId");
                    if (idIt == obj.end() || !idIt->second.isString())
                        continue;
                    std::string pkgId = idIt->second.toString();

                    auto verIt = obj.find("version");
                    if (verIt == obj.end() || !verIt->second.isString())
                        continue;
                    auto version = stdc::VersionNumber::fromString(verIt->second.toString());
                    if (version.isEmpty())
                        continue;

                    stdc::VersionNumber compatVersion = version;
                    auto                cvIt          = obj.find("compatVersion");
                    if (cvIt != obj.end() && cvIt->second.isString())
                        compatVersion = stdc::VersionNumber::fromString(cvIt->second.toString());

                    cachedIndexes[pkgId][version] = {fs::canonical(entry.path()), compatVersion};
                }
            }
        } catch (const std::exception &e) {
            pkgMgrLog.srtWarning(
                "refreshPackageIndexes: filesystem error while scanning context '%1': %2",
                ctxKey.toString(), e.what());
        }
    }

    void PackageManager::Impl::eraseContextState(const srt::core::ContextKey &ctxKey) {
        // TD-02: centralized retire of per-context state. Callers must hold
        // m_su_mtx (unique) — the 7 maps below are guarded by it.
        m_contextPackagePaths.erase(ctxKey);
        m_contextStates.erase(ctxKey);
        m_contextDependencyErrors.erase(ctxKey);
        m_contextModuleInfos.erase(ctxKey);
        m_contextDependencyResolved.erase(ctxKey);
        m_contextDependencyGraphs.erase(ctxKey);
        m_contextCachedIndexes.erase(ctxKey);
    }

    srt::core::Expected<PackageData *> PackageManager::Impl::openPackage(const fs::path &path) {
        // ROBUST-02: stdc::path::canonical uses error_code internally and
        // returns an empty path on failure. fs::is_directory is also called
        // with error_code to avoid filesystem_error exceptions on race or
        // permission errors.
        auto canonicalPath = stdc::path::canonical(path);
        std::error_code ec;
        if (canonicalPath.empty() || !fs::is_directory(canonicalPath, ec)) {
            return Error(ErrorCode::G2pFileSystemError, "Invalid package path: " + stdc::path::to_utf8(path));
        }

        {
            std::unique_lock lock(m_su_mtx);
            auto            &pkgMap = m_loadedPackageMap;
            if (auto it = pkgMap.pathIndexes.find(canonicalPath); it != pkgMap.pathIndexes.end()) {
                auto &pkg = *it->second;
                pkg.ref++;
                return pkg.spec;
            }
        }

        auto *pd = new PackageData(m_q);

        auto parseExp = pd->parse(canonicalPath, m_categories, nullptr);
        if (!parseExp) {
            delete pd;
            return parseExp.error();
        }

        // Get contributes from moduleSpecs
        llvm::SmallVector<srt::core::ModuleSpec *> contributes;
        for (const auto &[cat, byId] : pd->m_moduleSpecs) {
            for (const auto &[mid, spec] : byId) {
                contributes.push_back(spec);
                spec->_impl->m_packageId      = pd->m_id;
                spec->_impl->m_packageVersion = pd->m_version;
            }
        }

        // Check duplicates
        {
            std::unique_lock lock(m_su_mtx);
            auto            &pkgMap = m_loadedPackageMap;

            if (auto it = pkgMap.idIndexes.find(pd->m_id); it != pkgMap.idIndexes.end()) {
                auto &versionMap = it->second;
                if (auto it2 = versionMap.find(pd->m_version); it2 != versionMap.end()) {
                    pd->m_err = Error(ErrorCode::G2pFileSystemError,
                                      stdc::formatN("duplicate package %1[%2]", pd->m_id, pd->m_version.toString()));
                    m_resourcePackages.insert(pd);
                    return pd;
                }
            }
            if (auto it = m_pendingPackages.find(pd->m_id); it != m_pendingPackages.end()) {
                if (it->second.count(pd->m_version)) {
                    pd->m_err = Error(ErrorCode::G2pDependencyError, stdc::formatN("recursive dependency: %1[%2]",
                                                                                   pd->m_id, pd->m_version.toString()));
                    m_resourcePackages.insert(pd);
                    return pd;
                }
            }
            m_pendingPackages[pd->m_id][pd->m_version] = pd->m_path;
        }

        // Initialize contributes through categories
        {
            Error error1;
            bool  failed = false;
            int   i      = 0;
            for (; i < (int)contributes.size(); ++i) {
                auto *spec  = contributes[i];
                auto  catIt = m_categories.find(spec->category());
                if (catIt == m_categories.end()) {
                    error1 = Error(ErrorCode::G2pNotImplementedError, "category not found: " + spec->category());
                    failed = true;
                    break;
                }
                auto *cc = catIt->second;
                if (auto exp = cc->loadSpec(spec, srt::core::ModuleSpec::Initialized); !exp) {
                    error1 = Error(exp.error().code(), exp.error().message());
                    failed = true;
                    break;
                }
                spec->_impl->m_state = srt::core::ModuleSpec::Initialized;
            }
            if (failed) {
                for (; i >= 0; --i) {
                    auto *spec  = contributes[i];
                    auto  catIt = m_categories.find(spec->category());
                    if (catIt == m_categories.end())
                        continue;
                    auto *cc             = catIt->second;
                    std::ignore          = cc->loadSpec(spec, srt::core::ModuleSpec::Deleted);
                    spec->_impl->m_state = srt::core::ModuleSpec::Deleted;
                }
                pd->m_err = error1;
                std::unique_lock lock(m_su_mtx);
                auto            &pp = m_pendingPackages[pd->m_id];
                pp.erase(pd->m_version);
                if (pp.empty())
                    m_pendingPackages.erase(pd->m_id);
                m_resourcePackages.insert(pd);
                return pd;
            }
        }

        // Ready phase
        {
            Error error1;
            bool  failed = false;
            int   i      = 0;
            for (; i < (int)contributes.size(); ++i) {
                auto *spec = contributes[i];
                auto *cc   = m_categories.at(spec->category());
                if (auto exp = cc->loadSpec(spec, srt::core::ModuleSpec::Ready); !exp) {
                    error1 = Error(exp.error().code(), exp.error().message());
                    failed = true;
                    break;
                }
                spec->_impl->m_state = srt::core::ModuleSpec::Ready;
            }
            if (failed) {
                for (; i >= 0; --i) {
                    auto *spec  = contributes[i];
                    auto  catIt = m_categories.find(spec->category());
                    if (catIt == m_categories.end())
                        continue;
                    auto *cc             = catIt->second;
                    std::ignore          = cc->loadSpec(spec, srt::core::ModuleSpec::Finished);
                    spec->_impl->m_state = srt::core::ModuleSpec::Finished;
                }
                for (i = (int)contributes.size() - 1; i >= 0; i--) {
                    auto *spec  = contributes[i];
                    auto  catIt = m_categories.find(spec->category());
                    if (catIt == m_categories.end())
                        continue;
                    auto *cc             = catIt->second;
                    std::ignore          = cc->loadSpec(spec, srt::core::ModuleSpec::Deleted);
                    spec->_impl->m_state = srt::core::ModuleSpec::Deleted;
                }
                pd->m_err = error1;
                std::unique_lock lock(m_su_mtx);
                auto            &pp = m_pendingPackages[pd->m_id];
                pp.erase(pd->m_version);
                if (pp.empty())
                    m_pendingPackages.erase(pd->m_id);
                m_resourcePackages.insert(pd);
                return pd;
            }
        }

        pd->m_loaded = true;

        {
            LoadedPackageBlock pkg;
            pkg.spec        = pd;
            pkg.ref         = 1;
            pkg.contributes = std::move(contributes);

            std::unique_lock lock(m_su_mtx);
            auto            &pp = m_pendingPackages[pd->m_id];
            pp.erase(pd->m_version);
            if (pp.empty())
                m_pendingPackages.erase(pd->m_id);

            auto &[packages, pathIndexes, idIndexes, pointerIndexes] = m_loadedPackageMap;
            auto it                                                  = packages.insert(packages.end(), std::move(pkg));
            pathIndexes[pd->m_path]                                  = it;
            idIndexes[pd->m_id][pd->m_version]                       = it;
            pointerIndexes[pd]                                       = it;
        }

        return pd;
    }

    bool PackageManager::Impl::closePackage(PackageData *spec) {
        if (!spec->m_loaded) {
            std::unique_lock lock(m_su_mtx);
            auto             it = m_resourcePackages.find(spec);
            if (it == m_resourcePackages.end())
                return false;
            m_resourcePackages.erase(it);
            delete spec;
            return true;
        }

        LoadedPackageBlock pkgToClose;
        {
            std::unique_lock lock(m_su_mtx);
            auto &[packages, pathIndexes, idIndexes, pointerIndexes] = m_loadedPackageMap;
            auto it                                                  = pointerIndexes.find(spec);
            if (it == pointerIndexes.end())
                return false;
            auto  it1 = it->second;
            auto &pkg = *it1;
            pkg.ref--;
            if (pkg.ref != 0)
                return true;
            pkgToClose = std::move(pkg);
            packages.erase(it1);
            pointerIndexes.erase(it);
            pathIndexes.erase(spec->m_path);
            auto it2 = idIndexes.find(spec->m_id);
            if (it2 != idIndexes.end()) {
                it2->second.erase(spec->m_version);
                if (it2->second.empty())
                    idIndexes.erase(it2);
            }
        }

        for (auto it = pkgToClose.contributes.rbegin(); it != pkgToClose.contributes.rend(); ++it) {
            auto &cc              = m_categories.at((*it)->category());
            std::ignore           = cc->loadSpec(*it, srt::core::ModuleSpec::Finished);
            (*it)->_impl->m_state = srt::core::ModuleSpec::Finished;
        }
        for (auto it = pkgToClose.contributes.rbegin(); it != pkgToClose.contributes.rend(); ++it) {
            auto &cc              = m_categories.at((*it)->category());
            std::ignore           = cc->loadSpec(*it, srt::core::ModuleSpec::Deleted);
            (*it)->_impl->m_state = srt::core::ModuleSpec::Deleted;
        }

        for (auto it = pkgToClose.linked.rbegin(); it != pkgToClose.linked.rend(); ++it) {
            std::ignore = closePackage(*it);
        }

        delete spec;
        return true;
    }

    void PackageManager::Impl::closeAllLoadedPackages() {
        while (!m_loadedPackageMap.packages.empty()) {
            auto spec   = m_loadedPackageMap.packages.back().spec;
            std::ignore = closePackage(spec);
        }
        // Clean up resource packages
        for (auto *rp : m_resourcePackages) {
            delete rp;
        }
        m_resourcePackages.clear();
    }

    // ============================================================================
    // PackageManager
    // ============================================================================

    PackageManager::PackageManager(Impl &impl) : _impl(&impl) {
        // Register a process-wide shutdown hook so that when ANY Runtime
        // instance destructs (before plugin DLLs are unloaded), this
        // PackageManager's ObjectPool references are released while the
        // plugin DLLs are still loaded.
        //
        // This is necessary because Manager (a subclass that uses this
        // constructor path) is a static singleton whose destructor runs
        // AFTER the host's Runtime. Without this hook, the Manager
        // destructor would release Task/SessionFactory shared_ptrs whose
        // vtables reside in already-unloaded plugin DLLs, causing
        // use-after-free crashes in shared_ptr::_Decref.
        //
        // The hook captures `this` (raw pointer to the static singleton,
        // whose address is stable for the process lifetime). shutdown() is
        // idempotent, so a later destructor call is a no-op.
        //
        // NOTE: This is identical to the hook registration in the default
        // constructor; both paths must register it since Manager bypasses
        // the default constructor.
        srt::core::Runtime::setGlobalShutdownHook([this]() {
            _impl->shutdown();
        });
    }

    PackageManager::PackageManager() : srt::core::PluginFactory(), _impl(new Impl(this)) {
        // Register a process-wide shutdown hook so that when ANY Runtime
        // instance destructs (before plugin DLLs are unloaded), this
        // PackageManager's ObjectPool references are released while the
        // plugin DLLs are still loaded.
        //
        // This is necessary because PackageManager (via srt::g2p::Manager)
        // is a static singleton whose destructor runs AFTER the host's
        // Runtime (a local variable). Without this hook, the PackageManager
        // destructor would release Task/SessionFactory shared_ptrs whose
        // vtables reside in already-unloaded plugin DLLs, causing
        // use-after-free crashes in shared_ptr::_Decref.
        //
        // The hook captures `this` (raw pointer to the static singleton,
        // whose address is stable for the process lifetime). shutdown() is
        // idempotent, so a later destructor call is a no-op.
        srt::core::Runtime::setGlobalShutdownHook([this]() {
            _impl->shutdown();
        });
    }

    PackageManager::~PackageManager() = default;

    void PackageManager::shutdown() {
        auto &impl = *static_cast<Impl *>(_impl.get());
        impl.shutdown();
    }

    srt::core::ModuleCategory *PackageManager::category(const std::string_view &name) const {
        auto      &impl = *static_cast<Impl *>(_impl.get());
        const auto it   = impl.m_categories.find(name);
        if (it == impl.m_categories.end())
            return nullptr;
        return it->second;
    }

    std::vector<std::string> PackageManager::getDependencyErrors() const {
        auto                    &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock         lock(impl.m_su_mtx);
        std::vector<std::string> all;
        for (const auto &[_, errs] : impl.m_contextDependencyErrors) {
            all.insert(all.end(), errs.begin(), errs.end());
        }
        return all;
    }

    std::vector<std::string> PackageManager::getDependencyErrors(const srt::core::ContextKey &ctxKey) const {
        auto            &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.m_su_mtx);
        auto             it = impl.m_contextDependencyErrors.find(ctxKey);
        if (it == impl.m_contextDependencyErrors.end())
            return {};
        return it->second;
    }

    srt::core::Expected<void> PackageManager::addPackagePath(const std::string           &context,
                                                             const stdc::VersionNumber   &version,
                                                             const std::filesystem::path &path) {
        if (auto exp = ContextUtils::validateContextName(context); !exp)
            return exp.error();

        if (context.empty() && !version.isEmpty())
            return Error(ErrorCode::G2pValidationError, "R-8: Default context cannot have a version");
        if (!context.empty() && version.isEmpty())
            return Error(ErrorCode::G2pValidationError,
                         "Context '" + context + "': version cannot be empty for a versioned context");

        auto           &impl = *static_cast<Impl *>(_impl.get());
        std::error_code ec;
        if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
            return Error(
                ErrorCode::G2pFileSystemError,
                stdc::formatN("Package path does not exist or is not a directory: %1", stdc::path::to_utf8(path)));
        }

        auto canonical = fs::canonical(path, ec);
        if (ec) {
            return Error(ErrorCode::G2pFileSystemError,
                         stdc::formatN("Package path cannot be canonicalized: %1", stdc::path::to_utf8(path)));
        }
        srt::core::ContextKey ctxKey(context, version);

        std::unique_lock lock(impl.m_su_mtx);
        auto            &paths = impl.m_contextPackagePaths[ctxKey];
        for (const auto &existing : paths) {
            if (existing == canonical)
                return {};
        }
        paths.push_back(canonical);
        impl.m_packagePathsDirty = true;
        return {};
    }

    srt::core::Expected<void> PackageManager::setPackagePaths(const std::string                        &context,
                                                              const stdc::VersionNumber                &version,
                                                              const std::vector<std::filesystem::path> &paths) {
        if (auto exp = ContextUtils::validateContextName(context); !exp)
            return exp.error();

        if (context.empty() && !version.isEmpty())
            return Error(ErrorCode::G2pValidationError, "R-8: Default context cannot have a version");
        if (!context.empty() && version.isEmpty())
            return Error(ErrorCode::G2pValidationError,
                         "Context '" + context + "': version cannot be empty for a versioned context");

        auto                 &impl = *static_cast<Impl *>(_impl.get());
        srt::core::ContextKey ctxKey(context, version);
        std::unique_lock      lock(impl.m_su_mtx);
        auto                 &ctxPaths = impl.m_contextPackagePaths[ctxKey];
        ctxPaths.clear();
        for (const auto &path : paths) {
            if (!fs::is_directory(path))
                continue;
            ctxPaths.push_back(fs::canonical(path));
        }
        impl.m_packagePathsDirty = true;
        return {};
    }

    std::vector<std::filesystem::path> PackageManager::packagePaths(const std::string         &context,
                                                                    const stdc::VersionNumber &version) const {
        auto                 &impl = *static_cast<Impl *>(_impl.get());
        srt::core::ContextKey ctxKey(context, version);
        std::shared_lock      lock(impl.m_su_mtx);
        auto                  it = impl.m_contextPackagePaths.find(ctxKey);
        if (it == impl.m_contextPackagePaths.end())
            return {};
        return {it->second.begin(), it->second.end()};
    }

    std::vector<srt::core::ContextKey> PackageManager::contextKeys() const {
        auto                              &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock                   lock(impl.m_su_mtx);
        std::vector<srt::core::ContextKey> result;
        for (const auto &[ctxKey, _] : impl.m_contextPackagePaths)
            result.push_back(ctxKey);
        return result;
    }

    srt::core::Expected<size_t> PackageManager::removeContextsByPrefix(const std::string         &prefix,
                                                                       const stdc::VersionNumber &version) {
        // D-43: version-aware retire. Multi-version same-packageId hot reload
        // must retire only the contexts belonging to the retired version, not
        // every context under the prefix, to preserve coexisting versions
        // (D-24 multi-version coexistence).
        if (prefix.empty()) {
            return Error(ErrorCode::G2pValidationError,
                         "removeContextsByPrefix: prefix must not be empty (empty prefix "
                         "would match all contexts)");
        }

        auto &impl = *static_cast<Impl *>(_impl.get());

        std::vector<srt::core::ContextKey> matching;
        size_t                             removed = 0;
        {
            std::unique_lock lock(impl.m_su_mtx);

            if (impl.m_initialized) {
                return Error(ErrorCode::G2pAlreadyInitialized,
                             "removeContextsByPrefix: Manager::initialize() already "
                             "called; contexts are immutable. Restart the host process "
                             "for G2P changes.");
            }

            // Match ctxKey.context prefix AND ctxKey.version exactly. An empty
            // version matches only the default context (kOfficialContext).
            for (const auto &[ctxKey, _] : impl.m_contextPackagePaths) {
                if (ctxKey.context.starts_with(prefix) && ctxKey.version == version) {
                    matching.push_back(ctxKey);
                }
            }

            removed = matching.size();
            for (const auto &ctxKey : matching) {
                impl.eraseContextState(ctxKey);
            }

            if (removed > 0) {
                impl.m_packagePathsDirty = true;
            }
        }

        if (!matching.empty()) {
            std::unique_lock<std::shared_mutex> tasksLock(impl.m_tasks_mtx);
            for (auto &[category, ctxMap] : impl.m_tasks) {
                for (const auto &ctxKey : matching) {
                    ctxMap.erase(ctxKey);
                }
            }
        }

        return removed;
    }

    ContextState PackageManager::contextState(const srt::core::ContextKey &ctxKey) const {
        auto            &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.m_su_mtx);
        const auto       it = impl.m_contextStates.find(ctxKey);
        if (it != impl.m_contextStates.end())
            return it->second;
        if (impl.m_contextPackagePaths.find(ctxKey) != impl.m_contextPackagePaths.end())
            return ContextState::Pending;
        return ContextState::NotRegistered;
    }

    std::vector<srt::core::ContextKey> PackageManager::failedContexts() const {
        auto                              &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock                   lock(impl.m_su_mtx);
        std::vector<srt::core::ContextKey> result;
        for (const auto &[ctxKey, state] : impl.m_contextStates) {
            if (state == ContextState::Failed && !ctxKey.isDefault())
                result.push_back(ctxKey);
        }
        return result;
    }

    // -- Package management ---------------------------------------------------

    srt::core::Expected<Package> PackageManager::open(const std::filesystem::path &path) {
        auto &impl   = *static_cast<Impl *>(_impl.get());
        auto  result = impl.openPackage(path);
        if (!result)
            return result.error();
        return Package(result.get());
    }

    Package PackageManager::find(const std::string_view &id, const stdc::VersionNumber &version) const {
        auto            &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.m_su_mtx);
        auto            &pkgMap = impl.m_loadedPackageMap;
        auto             it     = pkgMap.idIndexes.find(id);
        if (it == pkgMap.idIndexes.end())
            return {};
        auto it2 = it->second.find(version);
        if (it2 == it->second.end())
            return {};
        return Package(it2->second->spec);
    }

    std::vector<Package> PackageManager::find(const std::string_view &id) const {
        auto            &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.m_su_mtx);
        auto            &pkgMap = impl.m_loadedPackageMap;
        auto             it     = pkgMap.idIndexes.find(id);
        if (it == pkgMap.idIndexes.end())
            return {};
        std::vector<Package> res;
        for (const auto &[_, iter] : it->second)
            res.push_back(Package(iter->spec));
        return res;
    }

    std::vector<Package> PackageManager::packages() const {
        auto                &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock     lock(impl.m_su_mtx);
        std::vector<Package> res;
        for (const auto &item : impl.m_loadedPackageMap.packages)
            res.push_back(Package(item.spec));
        return res;
    }

    srt::core::Expected<srt::core::NO<Task>>
        PackageManager::createModuleTask(const srt::dependency::ModuleMetadata &moduleInfo, const Package &pkg) const {
        static srt::core::LogCategory pkgMgrLog("PackageManager");

        auto *moduleSpec = pkg.moduleSpec(moduleInfo.type, moduleInfo.moduleId);
        if (!moduleSpec) {
            pkgMgrLog.srtWarning("createModuleTask: moduleSpec not found module=%1", moduleInfo.moduleId);
            return Error::g2pError(ErrorCode::G2pPackageNotFound,
                                   stdc::formatN("Module not found: package=%1, moduleId=%2, type=%3",
                                                 moduleInfo.packageId, moduleInfo.moduleId, moduleInfo.type),
                                   {}, moduleInfo.packageId);
        }

        moduleSpec->_impl->m_contextKey = srt::core::ContextKey(moduleInfo.context, moduleInfo.contextVersion);

        auto *taskPlugin = this->plugin<TaskPlugin>(moduleInfo.iid.c_str());
        if (!taskPlugin) {
            pkgMgrLog.srtWarning("createModuleTask: task plugin not found iid=%1", moduleInfo.iid);
            return Error::g2pError(ErrorCode::G2pRuntimeError,
                                   stdc::formatN("Task plugin not found for iid: %1", moduleInfo.iid), {},
                                   moduleInfo.packageId);
        }

        auto taskExp = taskPlugin->createTask(moduleSpec);
        if (!taskExp) {
            pkgMgrLog.srtWarning("createModuleTask: createTask failed module=%1: %2",
                                 moduleInfo.moduleId, taskExp.error().message());
            return taskExp.error();
        }

        auto task = taskExp.take();
        task->setMgr(const_cast<PackageManager *>(this));

        // Catch C++ exceptions from task->initialize() (e.g. from third-party
        // libraries like cpp-pinyin). Without this, an exception would
        // propagate through Manager::initialize() → SynthrtEngine::initialize()
        // → InitInferEngineTask::runTask(), preventing the Task::finished
        // signal from firing and leaving module status stuck at Loading.
        try {
            auto initExp = task->initialize();
            if (!initExp) {
                pkgMgrLog.srtWarning("createModuleTask: task->initialize failed module=%1: %2",
                                     moduleInfo.moduleId, initExp.error().message());
                return initExp.error();
            }
        } catch (const std::exception &e) {
            pkgMgrLog.srtWarning("createModuleTask: task->initialize threw exception module=%1: %2",
                                 moduleInfo.moduleId, e.what());
            return Error::g2pError(ErrorCode::G2pInitializationError,
                                   stdc::formatN("Task initialization threw exception: %1", e.what()),
                                   {}, moduleInfo.packageId);
        } catch (...) {
            pkgMgrLog.srtWarning("createModuleTask: task->initialize threw unknown exception module=%1",
                                 moduleInfo.moduleId);
            return Error::g2pError(ErrorCode::G2pInitializationError,
                                   "Task initialization threw unknown exception",
                                   {}, moduleInfo.packageId);
        }

        return task;
    }

    std::vector<srt::dependency::ModuleMetadata> PackageManager::getModuleMetadatas(const std::string &context) {
        return getModuleMetadatas(srt::core::ContextKey(context));
    }

    std::vector<srt::dependency::ModuleMetadata>
        PackageManager::getModuleMetadatas(const srt::core::ContextKey &ctxKey) {
        auto &impl = *static_cast<Impl *>(_impl.get());

        // Check cache
        {
            std::shared_lock lock(impl.m_su_mtx);
            auto             it = impl.m_contextModuleInfos.find(ctxKey);
            if (it != impl.m_contextModuleInfos.end() && !impl.m_packagePathsDirty)
                return it->second;
        }

        // Refresh package indexes if dirty
        std::unique_lock lock(impl.m_su_mtx);
        if (impl.m_packagePathsDirty) {
            impl.refreshPackageIndexes(ctxKey);
        }

        // Scan for modules
        std::vector<srt::dependency::ModuleMetadata> metadatas;
        auto                                         pathsIt = impl.m_contextPackagePaths.find(ctxKey);
        if (pathsIt == impl.m_contextPackagePaths.end())
            return {};

        // ROBUST-02: Wrap filesystem operations in try-catch (see
        // refreshPackageIndexes for rationale). On error, log a warning and
        // return whatever was collected.
        try {
            for (const auto &basePath : pathsIt->second) {
                if (!fs::is_directory(basePath))
                    continue;
                for (const auto &entry : fs::directory_iterator(basePath)) {
                    if (!entry.is_directory())
                        continue;
                    auto packageDir = entry.path();

                    auto pkgJson = packageDir / "package.json";
                    if (!fs::exists(pkgJson))
                        continue;

                    auto expObj = PackageData::readDesc(pkgJson);
                    if (!expObj) {
                        // ROBUST-05: log skip reason instead of silently continuing.
                        pkgMgrLog.srtWarning(
                            "PackageManager: skipping package at '%1': failed to read package.json: %2",
                            stdc::path::to_utf8(pkgJson), expObj.errorString());
                        continue;
                    }
                    auto root = expObj.take();

                    // packageId
                    auto idIt = root.find("packageId");
                    if (idIt == root.end() || !idIt->second.isString())
                        continue;
                    std::string packageId = idIt->second.toString();

                    // version
                    auto verIt = root.find("version");
                    if (verIt == root.end() || !verIt->second.isString())
                        continue;
                    auto pkgVersion = verIt->second.toString();

                    // level
                    int  level = 1;
                    auto lvlIt = root.find("level");
                    if (lvlIt != root.end() && lvlIt->second.isNumber())
                        level = lvlIt->second.toInt();

                    // modules
                    auto modIt = root.find("modules");
                    if (modIt == root.end() || !modIt->second.isObject())
                        continue;

                    for (const auto &[type, moduleListVal] : modIt->second.toObject()) {
                        if (!moduleListVal.isArray())
                            continue;
                        for (const auto &moduleEntry : moduleListVal.toArray()) {
                            if (!moduleEntry.isObject())
                                continue;
                            const auto &entryObj = moduleEntry.toObject();

                            auto midIt = entryObj.find("moduleId");
                            if (midIt == entryObj.end() || !midIt->second.isString())
                                continue;

                            auto classIt = entryObj.find("class");
                            if (classIt == entryObj.end() || !classIt->second.isString())
                                continue;

                            auto configIt = entryObj.find("configuration");

                            srt::dependency::ModuleMetadata md;
                            md.context        = ctxKey.context;
                            md.contextVersion = ctxKey.version;
                            md.packageId      = packageId;
                            md.moduleId       = midIt->second.toString();
                            md.type           = type;
                            md.iid            = classIt->second.toString();
                            if (configIt != entryObj.end() && configIt->second.isString())
                                md.configuration = configIt->second.toString();
                            md.packagePath = packageDir;
                            md.version     = pkgVersion;
                            md.level       = level;

                            // Dependencies
                            auto depIt = entryObj.find("dependencies");
                            if (depIt != entryObj.end() && depIt->second.isArray()) {
                                for (const auto &depVal : depIt->second.toArray()) {
                                    if (!depVal.isObject())
                                        continue;
                                    const auto                            &depObj = depVal.toObject();
                                    srt::dependency::DependencyRequirement req;
                                    auto                                   dpIt = depObj.find("packageId");
                                    if (dpIt != depObj.end() && dpIt->second.isString())
                                        req.packageId = dpIt->second.toString();
                                    auto dmIt = depObj.find("moduleId");
                                    if (dmIt != depObj.end() && dmIt->second.isString())
                                        req.moduleId = dmIt->second.toString();
                                    auto dlIt = depObj.find("level");
                                    if (dlIt != depObj.end() && dlIt->second.isNumber())
                                        req.level = dlIt->second.toInt();
                                    auto dvIt = depObj.find("version");
                                    if (dvIt != depObj.end() && dvIt->second.isString())
                                        req.versionRange = dvIt->second.toString();
                                    md.requirements.push_back(req);
                                }
                            }

                            metadatas.push_back(md);
                        }
                    }
                }
            }
        } catch (const std::exception &e) {
            pkgMgrLog.srtWarning(
                "getModuleMetadatas: filesystem error while scanning context '%1': %2",
                ctxKey.toString(), e.what());
        }

        impl.m_contextModuleInfos[ctxKey] = metadatas;
        return metadatas;
    }

} // namespace srt::g2p
