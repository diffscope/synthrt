#include <synthrt/G2P/Core/PackageManager.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

#include <synthrt/G2P/Package/Package.h>
#include <synthrt/G2P/Support/ContextUtils.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/TaskPlugin.h>

#include "Core/PackageManager_p.h"
#include "Package/Package_p.h"
#include "../../Core/Module/Module_p.h"

namespace fs = std::filesystem;

namespace srt::g2p {

    namespace {
        /// Minimal ModuleSpec subclass for the "g2p" category.
        /// G2pCategory is declared as friend so parseSpec can access the
        /// protected _impl member inherited from ModuleSpec.
        class G2pCategory;

        class G2pSpec : public srt::core::ModuleSpec {
        public:
            G2pSpec() : srt::core::ModuleSpec("g2p") {}
            friend class G2pCategory;
        };

        /// Minimal ModuleSpec subclass for the "dict" category.
        class DictCategory;

        class DictSpec : public srt::core::ModuleSpec {
        public:
            DictSpec() : srt::core::ModuleSpec("dict") {}
            friend class DictCategory;
        };

        /// Concrete ModuleCategory for the "g2p" category.
        /// The G2P PackageManager is a standalone singleton (no Runtime),
        /// so categories are created directly with the manager pointer.
        class G2pCategory : public srt::core::ModuleCategory {
        public:
            explicit G2pCategory(void *mgr)
                : srt::core::ModuleCategory("g2p", mgr) {}

            std::string key() const override { return "g2p"; }
            std::string category() const override { return "g2p"; }

            srt::core::Expected<srt::core::ModuleSpec *>
            parseSpec(const std::filesystem::path &basePath,
                      const srt::core::JsonValue &config) const override;
        };

        /// Concrete ModuleCategory for the "dict" category.
        class DictCategory : public srt::core::ModuleCategory {
        public:
            explicit DictCategory(void *mgr)
                : srt::core::ModuleCategory("dict", mgr) {}

            std::string key() const override { return "dict"; }
            std::string category() const override { return "dict"; }

            srt::core::Expected<srt::core::ModuleSpec *>
            parseSpec(const std::filesystem::path &basePath,
                      const srt::core::JsonValue &config) const override;
        };

        /// Load a config file referenced by "configuration" in a module entry.
        /// Returns the inner "configuration" sub-object and updates apiLevel.
        /// This is a free helper (no friend access needed - only uses public JSON API).
        struct LoadedConfig {
            srt::core::JsonObject configuration;
            int apiLevel = 1;
        };
        bool loadConfigFile(const std::filesystem::path &configPath, LoadedConfig &out) {
            auto expObj = PackageData::readDesc(configPath);
            if (!expObj) return false;
            auto configFile = expObj.take();
            auto cfgIt = configFile.find("configuration");
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
            int level = 1;
            // configuration: either a file path (string) or an inline object
            std::string configFileRel;
            srt::core::JsonObject inlineConfig;
            bool hasInlineConfig = false;
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
                        f.inlineConfig = it->second.toObject();
                        f.hasInlineConfig = true;
                    }
                }
            }
            return f;
        }

        srt::core::Expected<srt::core::ModuleSpec *>
        G2pCategory::parseSpec(const std::filesystem::path &basePath,
                               const srt::core::JsonValue &config) const {
            if (!config.isObject()) {
                return srt::core::Error{
                    srt::core::Error::InvalidFormat,
                    "g2p module config must be a JSON object"};
            }
            auto *spec = new G2pSpec();
            // G2pCategory is a friend of G2pSpec, so it can access the
            // protected _impl inherited from ModuleSpec (protected access
            // through the derived type G2pSpec*).
            auto &impl = *spec->_impl;

            const auto &obj = config.toObject();
            auto f = extractModuleEntryFields(obj);
            impl.id = std::move(f.id);
            impl.className = std::move(f.className);
            impl.apiLevel = f.level;

            // Default path is the package directory; when a config file is
            // referenced, relative paths inside it resolve relative to the
            // config file's parent directory (DiffSinger convention).
            std::filesystem::path resolvedPath = basePath;
            if (!f.configFileRel.empty()) {
                auto configFilePath = basePath / f.configFileRel;
                LoadedConfig lc;
                if (loadConfigFile(configFilePath, lc)) {
                    impl.manifestConfiguration = std::move(lc.configuration);
                    impl.apiLevel = lc.apiLevel;
                    resolvedPath = configFilePath.parent_path();
                }
            } else if (f.hasInlineConfig) {
                impl.manifestConfiguration = std::move(f.inlineConfig);
            }

            impl.path = resolvedPath;
            return spec;
        }

        srt::core::Expected<srt::core::ModuleSpec *>
        DictCategory::parseSpec(const std::filesystem::path &basePath,
                                const srt::core::JsonValue &config) const {
            if (!config.isObject()) {
                return srt::core::Error{
                    srt::core::Error::InvalidFormat,
                    "dict module config must be a JSON object"};
            }
            auto *spec = new DictSpec();
            auto &impl = *spec->_impl;

            const auto &obj = config.toObject();
            auto f = extractModuleEntryFields(obj);
            impl.id = std::move(f.id);
            impl.className = std::move(f.className);
            impl.apiLevel = f.level;

            std::filesystem::path resolvedPath = basePath;
            if (!f.configFileRel.empty()) {
                auto configFilePath = basePath / f.configFileRel;
                LoadedConfig lc;
                if (loadConfigFile(configFilePath, lc)) {
                    impl.manifestConfiguration = std::move(lc.configuration);
                    impl.apiLevel = lc.apiLevel;
                    resolvedPath = configFilePath.parent_path();
                }
            } else if (f.hasInlineConfig) {
                impl.manifestConfiguration = std::move(f.inlineConfig);
            }

            impl.path = resolvedPath;
            return spec;
        }
    } // namespace

    PackageManager::Impl::Impl(PackageManager *decl) : decl(decl) {
        categories["g2p"] = new G2pCategory(decl);
        categories["dict"] = new DictCategory(decl);
    }

    PackageManager::Impl::~Impl() {
        closeAllLoadedPackages();
        for (const auto &[_, cate] : categories) {
            delete cate;
        }
    }

    void PackageManager::Impl::refreshPackageIndexes(const srt::core::ContextKey &ctxKey) {
        auto &cachedIndexes = contextCachedIndexes[ctxKey];
        cachedIndexes.clear();
        auto pathsIt = contextPackagePaths.find(ctxKey);
        if (pathsIt == contextPackagePaths.end()) return;

        for (const auto &path : pathsIt->second) {
            if (!fs::is_directory(path)) continue;
            for (const auto &entry : fs::directory_iterator(path)) {
                if (!entry.is_directory()) continue;

                auto pkgJson = entry.path() / "package.json";
                if (!fs::exists(pkgJson)) continue;

                auto expObj = PackageData::readDesc(pkgJson);
                if (!expObj) continue;
                auto obj = expObj.take();

                auto idIt = obj.find("packageId");
                if (idIt == obj.end() || !idIt->second.isString()) continue;
                std::string pkgId = idIt->second.toString();

                auto verIt = obj.find("version");
                if (verIt == obj.end() || !verIt->second.isString()) continue;
                auto version = stdc::VersionNumber::fromString(verIt->second.toString());
                if (version.isEmpty()) continue;

                stdc::VersionNumber compatVersion = version;
                auto cvIt = obj.find("compatVersion");
                if (cvIt != obj.end() && cvIt->second.isString())
                    compatVersion = stdc::VersionNumber::fromString(cvIt->second.toString());

                cachedIndexes[pkgId][version] = {fs::canonical(entry.path()), compatVersion};
            }
        }
        packagePathsDirty = false;
    }

    srt::core::Expected<PackageData *> PackageManager::Impl::openPackage(const fs::path &path) {
        auto canonicalPath = stdc::path::canonical(path);
        if (canonicalPath.empty() || !fs::is_directory(canonicalPath)) {
            return Error(Error::FileSystemError, "Invalid package path: " + stdc::path::to_utf8(path));
        }

        {
            std::unique_lock lock(su_mtx);
            auto &pkgMap = loadedPackageMap;
            if (auto it = pkgMap.pathIndexes.find(canonicalPath); it != pkgMap.pathIndexes.end()) {
                auto &pkg = *it->second;
                pkg.ref++;
                return pkg.spec;
            }
        }

        auto *pd = new PackageData(decl);

        auto parseExp = pd->parse(canonicalPath, categories, nullptr);
        if (!parseExp) {
            delete pd;
            return parseExp.error();
        }

        // Get contributes from moduleSpecs
        llvm::SmallVector<srt::core::ModuleSpec *> contributes;
        for (const auto &[cat, byId] : pd->moduleSpecs) {
            for (const auto &[mid, spec] : byId) {
                contributes.push_back(spec);
                spec->_impl->packageId = pd->id;
                spec->_impl->packageVersion = pd->version;
            }
        }

        // Check duplicates
        {
            std::unique_lock lock(su_mtx);
            auto &pkgMap = loadedPackageMap;

            if (auto it = pkgMap.idIndexes.find(pd->id); it != pkgMap.idIndexes.end()) {
                auto &versionMap = it->second;
                if (auto it2 = versionMap.find(pd->version); it2 != versionMap.end()) {
                    auto err = Error(Error::FileSystemError,
                        stdc::formatN("duplicate package %1[%2]", pd->id, pd->version.toString()));
                    pd->err = err;
                    resourcePackages.insert(pd);
                    return pd;
                }
            }
            if (auto it = pendingPackages.find(pd->id); it != pendingPackages.end()) {
                if (it->second.count(pd->version)) {
                    auto err = Error(Error::DependencyError,
                        stdc::formatN("recursive dependency: %1[%2]", pd->id, pd->version.toString()));
                    pd->err = err;
                    resourcePackages.insert(pd);
                    return pd;
                }
            }
            pendingPackages[pd->id][pd->version] = pd->path;
        }

        // Initialize contributes through categories
        {
            Error error1;
            bool failed = false;
            int i = 0;
            for (; i < (int)contributes.size(); ++i) {
                auto *spec = contributes[i];
                auto catIt = categories.find(spec->category());
                if (catIt == categories.end()) {
                    error1 = Error(Error::NotImplementedError,
                        "category not found: " + spec->category());
                    failed = true;
                    break;
                }
                auto *cc = catIt->second;
                if (auto exp = cc->loadSpec(spec, srt::core::ModuleSpec::Initialized); !exp) {
                    error1 = Error(static_cast<Error::Type>(exp.error().type()), exp.error().message());
                    failed = true;
                    break;
                }
                spec->_impl->state = srt::core::ModuleSpec::Initialized;
            }
            if (failed) {
                for (; i >= 0; --i) {
                    auto *spec = contributes[i];
                    auto *cc = categories.at(spec->category());
                    std::ignore = cc->loadSpec(spec, srt::core::ModuleSpec::Deleted);
                    spec->_impl->state = srt::core::ModuleSpec::Deleted;
                }
                pd->err = error1;
                std::unique_lock lock(su_mtx);
                pendingPackages.erase(pd->id);
                if (pendingPackages[pd->id].empty())
                    pendingPackages.erase(pd->id);
                resourcePackages.insert(pd);
                return pd;
            }
        }

        // Ready phase
        {
            Error error1;
            bool failed = false;
            int i = 0;
            for (; i < (int)contributes.size(); ++i) {
                auto *spec = contributes[i];
                auto *cc = categories.at(spec->category());
                if (auto exp = cc->loadSpec(spec, srt::core::ModuleSpec::Ready); !exp) {
                    error1 = Error(static_cast<Error::Type>(exp.error().type()), exp.error().message());
                    failed = true;
                    break;
                }
                spec->_impl->state = srt::core::ModuleSpec::Ready;
            }
            if (failed) {
                for (; i >= 0; --i) {
                    auto *spec = contributes[i];
                    auto *cc = categories.at(spec->category());
                    std::ignore = cc->loadSpec(spec, srt::core::ModuleSpec::Finished);
                    spec->_impl->state = srt::core::ModuleSpec::Finished;
                }
                for (i = (int)contributes.size() - 1; i >= 0; i--) {
                    auto *spec = contributes[i];
                    auto *cc = categories.at(spec->category());
                    std::ignore = cc->loadSpec(spec, srt::core::ModuleSpec::Deleted);
                    spec->_impl->state = srt::core::ModuleSpec::Deleted;
                }
                pd->err = error1;
                std::unique_lock lock(su_mtx);
                auto &pp = pendingPackages[pd->id];
                pp.erase(pd->version);
                if (pp.empty()) pendingPackages.erase(pd->id);
                resourcePackages.insert(pd);
                return pd;
            }
        }

        pd->loaded = true;

        {
            LoadedPackageBlock pkg;
            pkg.spec = pd;
            pkg.ref = 1;
            pkg.contributes = std::move(contributes);

            std::unique_lock lock(su_mtx);
            auto &pp = pendingPackages[pd->id];
            pp.erase(pd->version);
            if (pp.empty()) pendingPackages.erase(pd->id);

            auto &[packages, pathIndexes, idIndexes, pointerIndexes] = loadedPackageMap;
            auto it = packages.insert(packages.end(), std::move(pkg));
            pathIndexes[pd->path] = it;
            idIndexes[pd->id][pd->version] = it;
            pointerIndexes[pd] = it;
        }

        return pd;
    }

    bool PackageManager::Impl::closePackage(PackageData *spec) {
        if (!spec->loaded) {
            std::unique_lock lock(su_mtx);
            auto it = resourcePackages.find(spec);
            if (it == resourcePackages.end()) return false;
            resourcePackages.erase(it);
            delete spec;
            return true;
        }

        LoadedPackageBlock pkgToClose;
        {
            std::unique_lock lock(su_mtx);
            auto &[packages, pathIndexes, idIndexes, pointerIndexes] = loadedPackageMap;
            auto it = pointerIndexes.find(spec);
            if (it == pointerIndexes.end()) return false;
            auto it1 = it->second;
            auto &pkg = *it1;
            pkg.ref--;
            if (pkg.ref != 0) return true;
            pkgToClose = std::move(pkg);
            packages.erase(it1);
            pointerIndexes.erase(it);
            pathIndexes.erase(spec->path);
            auto it2 = idIndexes.find(spec->id);
            if (it2 != idIndexes.end()) {
                it2->second.erase(spec->version);
                if (it2->second.empty()) idIndexes.erase(it2);
            }
        }

        for (auto it = pkgToClose.contributes.rbegin(); it != pkgToClose.contributes.rend(); ++it) {
            auto &cc = categories.at((*it)->category());
            std::ignore = cc->loadSpec(*it, srt::core::ModuleSpec::Finished);
            (*it)->_impl->state = srt::core::ModuleSpec::Finished;
        }
        for (auto it = pkgToClose.contributes.rbegin(); it != pkgToClose.contributes.rend(); ++it) {
            auto &cc = categories.at((*it)->category());
            std::ignore = cc->loadSpec(*it, srt::core::ModuleSpec::Deleted);
            (*it)->_impl->state = srt::core::ModuleSpec::Deleted;
        }

        for (auto it = pkgToClose.linked.rbegin(); it != pkgToClose.linked.rend(); ++it) {
            std::ignore = closePackage(*it);
        }

        delete spec;
        return true;
    }

    void PackageManager::Impl::closeAllLoadedPackages() {
        while (!loadedPackageMap.packages.empty()) {
            auto spec = loadedPackageMap.packages.back().spec;
            std::ignore = closePackage(spec);
        }
        // Clean up resource packages
        for (auto *rp : resourcePackages) {
            delete rp;
        }
        resourcePackages.clear();
    }

    // ============================================================================
    // PackageManager
    // ============================================================================

    PackageManager::PackageManager(Impl &impl) : _impl(&impl) {}

    PackageManager::PackageManager()
        : srt::core::PluginFactory(), _impl(new Impl(this)) {}

    PackageManager::~PackageManager() = default;

    srt::core::ModuleCategory *PackageManager::category(const std::string_view &name) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        const auto it = impl.categories.find(name);
        if (it == impl.categories.end()) return nullptr;
        return it->second;
    }

    std::vector<std::string> PackageManager::getDependencyErrors() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        std::vector<std::string> all;
        for (const auto &[_, errs] : impl.contextDependencyErrors) {
            all.insert(all.end(), errs.begin(), errs.end());
        }
        return all;
    }

    std::vector<std::string> PackageManager::getDependencyErrors(
        const srt::core::ContextKey &ctxKey) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        auto it = impl.contextDependencyErrors.find(ctxKey);
        if (it == impl.contextDependencyErrors.end()) return {};
        return it->second;
    }

    srt::core::Expected<void> PackageManager::addPackagePath(
        const std::string &context, const std::filesystem::path &path) {
        if (auto exp = ContextUtils::validateContextName(context); !exp)
            return exp.error();

        auto &impl = *static_cast<Impl *>(_impl.get());
        if (!fs::exists(path) || !fs::is_directory(path)) {
            return Error(Error::FileSystemError,
                stdc::formatN("Package path does not exist or is not a directory: %1", path));
        }

        auto canonical = fs::canonical(path);
        srt::core::ContextKey ctxKey(context);

        std::unique_lock lock(impl.su_mtx);
        auto &paths = impl.contextPackagePaths[ctxKey];
        for (const auto &existing : paths) {
            if (existing == canonical) return {};
        }
        paths.push_back(canonical);
        impl.packagePathsDirty = true;
        return {};
    }

    srt::core::Expected<void> PackageManager::addPackagePath(
        const std::string &context, const stdc::VersionNumber &version,
        const std::filesystem::path &path) {
        if (auto exp = ContextUtils::validateContextName(context); !exp)
            return exp.error();

        if (context.empty() && !version.isEmpty())
            return Error(Error::ValidationError, "R-8: Default context cannot have a version");
        if (!context.empty() && version.isEmpty())
            return Error(Error::ValidationError,
                "Context '" + context + "': version cannot be empty for a versioned context");

        auto &impl = *static_cast<Impl *>(_impl.get());
        if (!fs::exists(path) || !fs::is_directory(path)) {
            return Error(Error::FileSystemError,
                stdc::formatN("Package path does not exist or is not a directory: %1", path));
        }

        auto canonical = fs::canonical(path);
        srt::core::ContextKey ctxKey(context, version);

        std::unique_lock lock(impl.su_mtx);
        auto &paths = impl.contextPackagePaths[ctxKey];
        for (const auto &existing : paths) {
            if (existing == canonical) return {};
        }
        paths.push_back(canonical);
        impl.packagePathsDirty = true;
        return {};
    }

    srt::core::Expected<void> PackageManager::setPackagePaths(
        const std::string &context, const std::vector<std::filesystem::path> &paths) {
        if (auto exp = ContextUtils::validateContextName(context); !exp)
            return exp.error();

        auto &impl = *static_cast<Impl *>(_impl.get());
        srt::core::ContextKey ctxKey(context);
        std::unique_lock lock(impl.su_mtx);
        auto &ctxPaths = impl.contextPackagePaths[ctxKey];
        ctxPaths.clear();
        for (const auto &path : paths) {
            if (!fs::is_directory(path)) continue;
            ctxPaths.push_back(fs::canonical(path));
        }
        impl.packagePathsDirty = true;
        return {};
    }

    srt::core::Expected<void> PackageManager::setPackagePaths(
        const std::string &context, const stdc::VersionNumber &version,
        const std::vector<std::filesystem::path> &paths) {
        if (auto exp = ContextUtils::validateContextName(context); !exp)
            return exp.error();

        if (context.empty() && !version.isEmpty())
            return Error(Error::ValidationError, "R-8: Default context cannot have a version");
        if (!context.empty() && version.isEmpty())
            return Error(Error::ValidationError,
                "Context '" + context + "': version cannot be empty for a versioned context");

        auto &impl = *static_cast<Impl *>(_impl.get());
        srt::core::ContextKey ctxKey(context, version);
        std::unique_lock lock(impl.su_mtx);
        auto &ctxPaths = impl.contextPackagePaths[ctxKey];
        ctxPaths.clear();
        for (const auto &path : paths) {
            if (!fs::is_directory(path)) continue;
            ctxPaths.push_back(fs::canonical(path));
        }
        impl.packagePathsDirty = true;
        return {};
    }

    std::vector<std::filesystem::path> PackageManager::packagePaths(
        const std::string &context) const {
        return packagePaths(context, {});
    }

    std::vector<std::filesystem::path> PackageManager::packagePaths(
        const std::string &context, const stdc::VersionNumber &version) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        srt::core::ContextKey ctxKey(context, version);
        std::shared_lock lock(impl.su_mtx);
        auto it = impl.contextPackagePaths.find(ctxKey);
        if (it == impl.contextPackagePaths.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    std::vector<std::string> PackageManager::contexts() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        std::vector<std::string> result;
        for (const auto &[ctxKey, _] : impl.contextPackagePaths)
            result.push_back(ctxKey.context);
        return result;
    }

    std::vector<srt::core::ContextKey> PackageManager::contextKeys() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        std::vector<srt::core::ContextKey> result;
        for (const auto &[ctxKey, _] : impl.contextPackagePaths)
            result.push_back(ctxKey);
        return result;
    }

    ContextState PackageManager::contextState(const srt::core::ContextKey &ctxKey) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        const auto it = impl.contextStates.find(ctxKey);
        if (it != impl.contextStates.end()) return it->second;
        if (impl.contextPackagePaths.find(ctxKey) != impl.contextPackagePaths.end())
            return ContextState::Pending;
        return ContextState::NotRegistered;
    }

    std::vector<srt::core::ContextKey> PackageManager::failedContexts() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        std::vector<srt::core::ContextKey> result;
        for (const auto &[ctxKey, state] : impl.contextStates) {
            if (state == ContextState::Failed && !ctxKey.isDefault())
                result.push_back(ctxKey);
        }
        return result;
    }

    // -- Package management ---------------------------------------------------

    srt::core::Expected<Package> PackageManager::open(const std::filesystem::path &path) {
        auto &impl = *static_cast<Impl *>(_impl.get());
        auto result = impl.openPackage(path);
        if (!result) return result.error();
        return Package(result.get());
    }

    Package PackageManager::find(const std::string_view &id,
                                  const stdc::VersionNumber &version) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        auto &pkgMap = impl.loadedPackageMap;
        auto it = pkgMap.idIndexes.find(id);
        if (it == pkgMap.idIndexes.end()) return {};
        auto it2 = it->second.find(version);
        if (it2 == it->second.end()) return {};
        return Package(it2->second->spec);
    }

    std::vector<Package> PackageManager::find(const std::string_view &id) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        auto &pkgMap = impl.loadedPackageMap;
        auto it = pkgMap.idIndexes.find(id);
        if (it == pkgMap.idIndexes.end()) return {};
        std::vector<Package> res;
        for (const auto &[_, iter] : it->second)
            res.push_back(Package(iter->spec));
        return res;
    }

    std::vector<Package> PackageManager::packages() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock lock(impl.su_mtx);
        std::vector<Package> res;
        for (const auto &item : impl.loadedPackageMap.packages)
            res.push_back(Package(item.spec));
        return res;
    }

    srt::core::Expected<srt::core::NO<Task>> PackageManager::createModuleTask(
        const srt::dependency::ModuleMetadata &moduleInfo, const Package &pkg) const {
        auto *moduleSpec = pkg.moduleSpec(moduleInfo.type, moduleInfo.moduleId);
        if (!moduleSpec) {
            return Error(Error::FileSystemError,
                stdc::formatN("Module not found: package=%1, moduleId=%2, type=%3",
                    moduleInfo.packageId, moduleInfo.moduleId, moduleInfo.type));
        }

        moduleSpec->_impl->contextKey = srt::core::ContextKey(moduleInfo.context, moduleInfo.contextVersion);

        auto *taskPlugin = this->plugin<TaskPlugin>(moduleInfo.iid.c_str());
        if (!taskPlugin) {
            return Error(Error::RuntimeError,
                stdc::formatN("Task plugin not found for iid: %1", moduleInfo.iid));
        }

        auto taskExp = taskPlugin->createTask(moduleSpec);
        if (!taskExp) return taskExp.error();

        auto task = taskExp.take();
        auto initExp = task->initialize();
        if (!initExp) return initExp.error();

        return task;
    }

    std::vector<srt::dependency::ModuleMetadata> PackageManager::getModuleMetadatas(
        const std::string &context) {
        return getModuleMetadatas(srt::core::ContextKey(context));
    }

    std::vector<srt::dependency::ModuleMetadata> PackageManager::getModuleMetadatas(
        const srt::core::ContextKey &ctxKey) {
        auto &impl = *static_cast<Impl *>(_impl.get());

        // Check cache
        {
            std::shared_lock lock(impl.su_mtx);
            auto it = impl.contextModuleInfos.find(ctxKey);
            if (it != impl.contextModuleInfos.end() && !impl.packagePathsDirty)
                return it->second;
        }

        // Refresh package indexes if dirty
        std::unique_lock lock(impl.su_mtx);
        if (impl.packagePathsDirty) {
            impl.refreshPackageIndexes(ctxKey);
        }

        // Scan for modules
        std::vector<srt::dependency::ModuleMetadata> metadatas;
        auto pathsIt = impl.contextPackagePaths.find(ctxKey);
        if (pathsIt == impl.contextPackagePaths.end()) return {};

        for (const auto &basePath : pathsIt->second) {
            if (!fs::is_directory(basePath)) continue;
            for (const auto &entry : fs::directory_iterator(basePath)) {
                if (!entry.is_directory()) continue;
                auto packageDir = entry.path();

                auto pkgJson = packageDir / "package.json";
                if (!fs::exists(pkgJson)) continue;

                auto expObj = PackageData::readDesc(pkgJson);
                if (!expObj) continue;
                auto root = expObj.take();

                // packageId
                auto idIt = root.find("packageId");
                if (idIt == root.end() || !idIt->second.isString()) continue;
                std::string packageId = idIt->second.toString();

                // version
                auto verIt = root.find("version");
                if (verIt == root.end() || !verIt->second.isString()) continue;
                auto pkgVersion = verIt->second.toString();

                // level
                int level = 1;
                auto lvlIt = root.find("level");
                if (lvlIt != root.end() && lvlIt->second.isNumber())
                    level = lvlIt->second.toInt();

                // modules
                auto modIt = root.find("modules");
                if (modIt == root.end() || !modIt->second.isObject()) continue;

                for (const auto &[type, moduleListVal] : modIt->second.toObject()) {
                    if (!moduleListVal.isArray()) continue;
                    for (const auto &moduleEntry : moduleListVal.toArray()) {
                        if (!moduleEntry.isObject()) continue;
                        const auto &entryObj = moduleEntry.toObject();

                        auto midIt = entryObj.find("moduleId");
                        if (midIt == entryObj.end() || !midIt->second.isString()) continue;

                        auto classIt = entryObj.find("class");
                        if (classIt == entryObj.end() || !classIt->second.isString()) continue;

                        auto configIt = entryObj.find("configuration");

                        srt::dependency::ModuleMetadata md;
                        md.context = ctxKey.context;
                        md.contextVersion = ctxKey.version;
                        md.packageId = packageId;
                        md.moduleId = midIt->second.toString();
                        md.type = type;
                        md.iid = classIt->second.toString();
                        if (configIt != entryObj.end() && configIt->second.isString())
                            md.configuration = configIt->second.toString();
                        md.packagePath = packageDir;
                        md.version = pkgVersion;
                        md.level = level;

                        // Dependencies
                        auto depIt = entryObj.find("dependencies");
                        if (depIt != entryObj.end() && depIt->second.isArray()) {
                            for (const auto &depVal : depIt->second.toArray()) {
                                if (!depVal.isObject()) continue;
                                const auto &depObj = depVal.toObject();
                                srt::dependency::DependencyRequirement req;
                                auto dpIt = depObj.find("packageId");
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

        impl.contextModuleInfos[ctxKey] = metadatas;
        return metadatas;
    }

} // namespace srt::g2p
