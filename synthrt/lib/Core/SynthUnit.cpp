#include "SynthUnit.h"
#include "SynthUnit_p.h"

#include <mutex>

#include <stdcorelib/stlextra/algorithms.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/path.h>

#include "JSON.h"
#include "Contribute_p.h"
#include "PackageRef_p.h"

namespace fs = std::filesystem;

namespace srt {

    std::vector<ContribCategory *(*) (SynthUnit *)> SynthUnit::Impl::categoryFactories;

    SynthUnit::Impl::Impl(SynthUnit *decl) : PluginFactory::Impl(decl) {
        for (const auto &factory : categoryFactories) {
            auto category = factory(decl);
            categories[std::string(category->name())] = category;
            cateKeyMap[std::string(category->key())] = category;
        }
    }

    SynthUnit::Impl::~Impl() {
        closeAllLoadedPackages();

        stdc::delete_all(categories);
    }

    PackageData *SynthUnit::Impl::open(const std::filesystem::path &path, bool noLoad,
                                       Error *error) {
        __stdc_decl_t;
        auto canonicalPath = stdc::path::canonical(path);
        if (canonicalPath.empty() || !fs::is_directory(canonicalPath)) {
            *error = {
                Error::FileNotFound,
                stdc::formatN(R"(invalid package path "%1")", path),
            };
            return nullptr;
        }

        // Check package path
        if (!noLoad) {
            std::unique_lock<std::shared_mutex> lock(su_mtx);
            auto &pkgMap = loadedPackageMap;
            auto it = pkgMap.pathIndexes.find(canonicalPath);
            if (it != pkgMap.pathIndexes.end()) {
                auto &pkg = *it->second;
                pkg.ref++;
                return pkg.spec;
            }
        }

        // Parse spec
        auto spec = new PackageData(&decl);
        std::vector<ContribSpec *> contributes;

        if (!spec->parse(canonicalPath, cateKeyMap, &contributes, error)) {
            delete spec;
            stdc::delete_all(contributes); // Maybe redundant
            return nullptr;
        }

        // Set parent
        for (const auto &contribute : std::as_const(contributes)) {
            contribute->_impl->package = spec;
        }

        // Add to package's data space
        for (const auto &contribute : std::as_const(contributes)) {
            spec->contributes[contribute->_impl->category][contribute->_impl->id] = contribute;
        }

        if (noLoad) {
            std::unique_lock<std::shared_mutex> lock(su_mtx);
            resourcePackages.insert(spec);
            return spec;
        }

        const auto &removePending = [this, spec] {
            auto it = pendingPackages.find(spec->id);
            auto &versionSet = it->second;
            versionSet.erase(spec->version);
            if (versionSet.empty()) {
                pendingPackages.erase(it);
            }
        };

        // Check duplications
        do {
            std::unique_lock<std::shared_mutex> lock(su_mtx);
            auto &pkgMap = loadedPackageMap;
            Error error1;

            // Check if a package with same id and version but different path is loaded
            {
                auto it = pkgMap.idIndexes.find(spec->id);
                if (it != pkgMap.idIndexes.end()) {
                    const auto &versionMap = it->second;
                    auto it2 = versionMap.find(spec->version);
                    if (it2 != versionMap.end()) {
                        auto pkg = *it2->second;
                        error1 = {
                            Error::FileDuplicated,
                            stdc::formatN(R"(duplicated package "%1[%2]" in "%3" is loaded)",
                                          spec->id, spec->version.toString(), pkg.spec->path),
                        };
                        goto out_dup;
                    }
                }
            }

            // Check pending list
            {
                auto it = pendingPackages.find(spec->id);
                if (it != pendingPackages.end()) {
                    const auto &versionMap = it->second;
                    auto it2 = versionMap.find(spec->version);
                    if (it2 != versionMap.end()) {
                        error1 = {
                            Error::RecursiveDependency,
                            stdc::formatN(
                                R"(recursive dependency chain detected: package "%1[%2]" in %3 is being loaded)",
                                spec->id, spec->version.toString(), it2->second),
                        };
                        goto out_dup;
                    }
                }
            }

            pendingPackages[spec->id][spec->version] = spec->path;
            break;

        out_dup:
            spec->err = error1;
            resourcePackages.insert(spec);
            return spec;
        } while (false);

        // Refresh dependency cache if needed
        if (packagePathsDirty) {
            std::unique_lock<std::shared_mutex> lock(su_mtx);
            refreshPackageIndexes();
        }

        // Load dependencies
        std::vector<PackageData *> dependencies;
        auto closeDependencies = [&dependencies, this]() {
            for (auto it = dependencies.rbegin(); it != dependencies.rend(); ++it) {
                std::ignore = close(*it);
            }
        };
        auto searchDependencies =
            [this](const std::string &id,
                   const stdc::VersionNumber &version) -> std::vector<fs::path> {
            std::vector<fs::path> res;
            auto it = cachedPackageIndexesMap.find(id);
            if (it == cachedPackageIndexesMap.end()) {
                return {};
            }

            // Search precise version
            const auto &versionMap = it->second;
            {
                auto it2 = versionMap.find(version);
                if (it2 != versionMap.end()) {
                    res.emplace_back(it2->second.path);
                }
            }

            // Test from high version to low version
            for (auto it2 = versionMap.rbegin(); it2 != versionMap.rend(); ++it2) {
                if (it2->first < version) {
                    break;
                }
                const auto &brief = it2->second;
                if (brief.compatVersion <= version) {
                    res.emplace_back(it2->second.path);
                }
            }
            return res;
        };
        do {
            Error error1;
            for (const auto &dep : std::as_const(spec->dependencies)) {
                stdc::VersionNumber depVersion;

                // Try to load all matched packages
                bool success = false;
                auto depPaths = searchDependencies(dep.id, dep.version);
                for (auto it = depPaths.rbegin(); it != depPaths.rend(); ++it) {
                    const auto &depPath = *it;
                    Error error2;
                    auto depPkg = open(depPath, true, &error2);
                    if (!depPkg) {
                        continue; // ignore
                    }
                    dependencies.push_back(depPkg);
                    success = true;
                    break;
                }

                if (success) {
                    continue;
                }

                if (!dep.required) {
                    continue; // ignore
                }

                // Not found
                error1 = {
                    Error::FileNotFound,
                    stdc::formatN(R"(required package "%1[%2]" not found)", dep.id,
                                  dep.version.toString()),
                };
                goto out_deps;
            }
            break;

        out_deps:
            closeDependencies();
            spec->err = error1;

            std::unique_lock<std::shared_mutex> lock(su_mtx);
            removePending();
            resourcePackages.insert(spec);
            return spec;
        } while (false);

        // Initialize
        {
            Error error1;
            bool failed = false;
            int i = 0;
            for (; i < contributes.size(); ++i) {
                const auto &contribute = contributes[i];
                const auto &cateName = contribute->_impl->category;
                auto it = categories.find(cateName);
                if (it == categories.end()) {
                    error1 = {
                        Error::FeatureNotSupported,
                        stdc::formatN(R"(category "%1" not found)", cateName),
                    };
                    failed = true;
                    break;
                }
                const auto &cate = it->second;
                if (!cate->loadSpec(contribute, ContribSpec::Initialized, &error1)) {
                    i--;
                    failed = true;
                    break;
                }
                contribute->_impl->state = ContribSpec::Initialized;
            }

            if (failed) {
                // Delete
                for (; i >= 0; --i) {
                    const auto &contribute = contributes[i];
                    const auto &cate = categories.at(contribute->_impl->category);
                    Error error2;
                    std::ignore = cate->loadSpec(contribute, ContribSpec::Deleted, &error2);
                    contribute->_impl->state = ContribSpec::Deleted;
                }

                closeDependencies();
                spec->err = error1;

                std::unique_lock<std::shared_mutex> lock(su_mtx);
                removePending();
                resourcePackages.insert(spec);
                return spec;
            }
        }

        // Get ready
        {
            Error error1;
            bool failed = false;
            int i = 0;
            for (; i < contributes.size(); ++i) {
                const auto &contribute = contributes[i];
                const auto &cate = categories.at(contribute->_impl->category);
                if (!cate->loadSpec(contribute, ContribSpec::Ready, &error1)) {
                    i--;
                    failed = true;
                    break;
                }
                contribute->_impl->state = ContribSpec::Ready;
            }

            if (failed) {
                // Finish
                for (; i >= 0; --i) {
                    const auto &contribute = contributes[i];
                    const auto &cate = categories.at(contribute->_impl->category);
                    Error error2;
                    std::ignore = cate->loadSpec(contribute, ContribSpec::Finished, &error2);
                    contribute->_impl->state = ContribSpec::Finished;
                }

                // Delete
                for (i = int(contributes.size()) - 1; i >= 0; i--) {
                    const auto &contribute = contributes[i];
                    const auto &cate = categories.at(contribute->_impl->category);
                    Error error2;
                    std::ignore = cate->loadSpec(contribute, ContribSpec::Deleted, &error2);
                    contribute->_impl->state = ContribSpec::Deleted;
                }

                closeDependencies();
                spec->err = error1;

                std::unique_lock<std::shared_mutex> lock(su_mtx);
                removePending();
                resourcePackages.insert(spec);
                return spec;
            }
        }

        spec->loaded = true;

        // Add to link map
        {
            Impl::LoadedPackageBlock pkg;
            pkg.spec = spec;
            pkg.ref = 1;
            pkg.contributes = std::move(contributes);
            pkg.linked = std::move(dependencies);

            std::unique_lock<std::shared_mutex> lock(su_mtx);
            removePending();
            auto &pkgMap = loadedPackageMap;
            auto it = pkgMap.packages.insert(pkgMap.packages.end(), pkg);
            pkgMap.pathIndexes[spec->path] = it;
            pkgMap.idIndexes[spec->id][spec->version] = it;
            pkgMap.pointerIndexes[spec] = it;
        }
        return spec;
    }

    bool SynthUnit::Impl::close(PackageData *spec) {
        if (!spec->loaded) {
            std::unique_lock<std::shared_mutex> lock(su_mtx);
            auto it = resourcePackages.find(spec);
            if (it == resourcePackages.end()) {
                return false;
            }

            resourcePackages.erase(it);
            delete spec;
            return true;
        }

        Impl::LoadedPackageBlock pkgToClose;
        {
            std::unique_lock<std::shared_mutex> lock(su_mtx);
            auto &pkgMap = loadedPackageMap;
            auto it = pkgMap.pointerIndexes.find(spec);
            if (it == pkgMap.pointerIndexes.end()) {
                return false;
            }

            auto it1 = it->second;
            auto &pkg = *it1;
            pkg.ref--;
            if (pkg.ref != 0) {
                return true;
            }
            pkgToClose = std::move(pkg);

            pkgMap.packages.erase(it1);
            pkgMap.pointerIndexes.erase(it);
            pkgMap.pathIndexes.erase(spec->path);

            // Remove id indexes
            auto it2 = pkgMap.idIndexes.find(spec->id);
            auto &versionMap = it2->second;
            versionMap.erase(spec->version);
            if (versionMap.empty()) {
                pkgMap.idIndexes.erase(it2);
            }
        }

        // Finish and delete
        {
            // Finish
            for (auto it = pkgToClose.contributes.rbegin(); it != pkgToClose.contributes.rend();
                 ++it) {
                const auto &contribute = *it;
                const auto &cate = categories.at(contribute->_impl->category);
                Error error2;
                std::ignore = cate->loadSpec(contribute, ContribSpec::Finished, &error2);
                contribute->_impl->state = ContribSpec::Finished;
            }

            // Delete
            for (auto it = pkgToClose.contributes.rbegin(); it != pkgToClose.contributes.rend();
                 ++it) {
                const auto &contribute = *it;
                const auto &cate = categories.at(contribute->_impl->category);
                Error error2;
                std::ignore = cate->loadSpec(contribute, ContribSpec::Deleted, &error2);
                contribute->_impl->state = ContribSpec::Deleted;
            }
        }

        // Unload dependencies
        for (auto it = pkgToClose.linked.rbegin(); it != pkgToClose.linked.rend(); ++it) {
            close(*it);
        }

        delete spec;
        return true;
    }

    void SynthUnit::Impl::closeAllLoadedPackages() {
        while (!loadedPackageMap.packages.empty()) {
            auto spec = loadedPackageMap.packages.back().spec;
            std::ignore = close(spec);
        }
    }

    void SynthUnit::Impl::refreshPackageIndexes() {
        cachedPackageIndexesMap.clear();
        for (const auto &path : std::as_const(packagePaths)) {
            try {
                for (const auto &entry : fs::directory_iterator(path)) {
                    const auto filename = entry.path().filename();
                    if (!entry.is_directory()) {
                        continue;
                    }

                    JsonObject obj;
                    Error error;
                    if (!PackageData::readDesc(entry.path(), &obj, &error)) {
                        continue;
                    }

                    // Search id, version, compatVersion
                    std::string id_;
                    stdc::VersionNumber version_;
                    stdc::VersionNumber compatVersion_;

                    // id
                    {
                        auto it = obj.find("id");
                        if (it == obj.end()) {
                            continue;
                        }
                        id_ = it->second.toString();
                        if (!ContribLocator::isValidLocator(id_)) {
                            continue;
                        }
                    }
                    // version
                    {
                        auto it = obj.find("version");
                        if (it == obj.end()) {
                            continue;
                        }
                        version_ = stdc::VersionNumber::fromString(it->second.toString());
                    }
                    // compatVersion
                    {
                        auto it = obj.find("compatVersion");
                        if (it != obj.end()) {
                            compatVersion_ = stdc::VersionNumber::fromString(it->second.toString());
                        } else {
                            compatVersion_ = version_;
                        }
                    }

                    // Store
                    cachedPackageIndexesMap[id_][version_] = {fs::canonical(entry.path()),
                                                              compatVersion_};
                }
            } catch (...) {
            }
        }

        packagePathsDirty = false;
    }

    SynthUnit::SynthUnit() : PluginFactory(*new Impl(this)) {
    }

    SynthUnit::~SynthUnit() = default;

    ContribCategory *SynthUnit::category(const std::string_view &name) const {
        __stdc_impl_t;
        auto it = impl.categories.find(name);
        if (it == impl.categories.end()) {
            return nullptr;
        }
        return it->second;
    }

    void SynthUnit::addPackagePaths(stdc::array_view<std::filesystem::path> paths) {
        __stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.su_mtx);
        for (const auto &path : paths) {
            if (!fs::is_directory(path)) {
                continue;
            }
            impl.packagePaths.push_back(fs::canonical(path));
            if (!impl.packagePathsDirty) {
                impl.packagePathsDirty = true;
            }
        }
    }

    void SynthUnit::setPackagePaths(stdc::array_view<std::filesystem::path> paths) {
        __stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.su_mtx);
        impl.packagePaths.clear();
        for (const auto &path : paths) {
            if (!fs::is_directory(path)) {
                continue;
            }
            impl.packagePaths.push_back(fs::canonical(path));
            if (!impl.packagePathsDirty) {
                impl.packagePathsDirty = true;
            }
        }
    }

    std::vector<std::filesystem::path> SynthUnit::packagePaths() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.su_mtx);
        return impl.packagePaths;
    }

    PackageRef SynthUnit::open(const std::filesystem::path &path, bool noLoad, Error *err) {
        __stdc_impl_t;
        auto result = impl.open(path, noLoad, err);
        if (!result) {
            return PackageRef();
        }
        return PackageRef(result);
    }

    PackageRef SynthUnit::find(const std::string_view &id,
                               const stdc::VersionNumber &version) const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.su_mtx);
        auto &pkgMap = impl.loadedPackageMap;
        auto it = pkgMap.idIndexes.find(id);
        if (it == pkgMap.idIndexes.end()) {
            return PackageRef();
        }

        auto &versionMap = it->second;
        auto it2 = versionMap.find(version);
        if (it2 == versionMap.end()) {
            return PackageRef();
        }
        return PackageRef((*it2->second).spec);
    }

    std::vector<PackageRef> SynthUnit::find(const std::string_view &id) const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.su_mtx);
        auto &pkgMap = impl.loadedPackageMap;
        auto it = pkgMap.idIndexes.find(id);
        if (it == pkgMap.idIndexes.end()) {
            return {};
        }

        auto &versionMap = it->second;
        std::vector<PackageRef> res;
        res.reserve(versionMap.size());
        for (const auto &pair : versionMap) {
            res.push_back(PackageRef(pair.second->spec));
        }
        return res;
    }

    std::vector<PackageRef> SynthUnit::packages() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.su_mtx);
        auto &list = impl.loadedPackageMap.packages;

        std::vector<PackageRef> res;
        res.reserve(list.size());
        for (const auto &item : list) {
            res.push_back(PackageRef(item.spec));
        }
        return res;
    }

    void SynthUnit::registerCategoryFactory(ContribCategory *(*fac)(SynthUnit *) ) {
        Impl::categoryFactories.push_back(fac);
    }

}