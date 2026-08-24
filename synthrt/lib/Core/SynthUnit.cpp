#include "SynthUnit.h"
#include "SynthUnit_p.h"

#include <utility>

#include "ContribCategory_p.h"
#include "ContribLocator.h"
#include "PackageLoader_p.h"

namespace srt {

    SynthUnit::SynthUnit() : _impl(std::make_unique<Impl>()) {
    }

    SynthUnit::SynthUnit(SynthUnit &&RHS) noexcept : _impl(std::move(RHS._impl)) {
        if (_impl) {
            for (auto &item : _impl->categories) {
                item.second->_impl->synthUnit = this;
            }
            for (auto &idEntry : _impl->packages) {
                for (auto &versionEntry : idEntry.second) {
                    if (auto package = versionEntry.second.lock()) {
                        package->synthUnit = this;
                    }
                }
            }
        }
    }

    SynthUnit &SynthUnit::operator=(SynthUnit &&RHS) noexcept {
        if (this == &RHS) {
            return *this;
        }
        _impl = std::move(RHS._impl);
        if (_impl) {
            for (auto &item : _impl->categories) {
                item.second->_impl->synthUnit = this;
            }
            for (auto &idEntry : _impl->packages) {
                for (auto &versionEntry : idEntry.second) {
                    if (auto package = versionEntry.second.lock()) {
                        package->synthUnit = this;
                    }
                }
            }
        }
        return *this;
    }

    SynthUnit::~SynthUnit() = default;

    ContribCategory *SynthUnit::category(std::string_view name) {
        if (!_impl) {
            return nullptr;
        }
        const auto it = _impl->categories.find(name);
        return it == _impl->categories.end() ? nullptr : it->second.get();
    }

    const ContribCategory *SynthUnit::category(std::string_view name) const {
        if (!_impl) {
            return nullptr;
        }
        const auto it = _impl->categories.find(name);
        return it == _impl->categories.end() ? nullptr : it->second.get();
    }

    Expected<void> SynthUnit::addCategory(std::unique_ptr<ContribCategory> category) {
        if (!_impl || !category) {
            return Error(Error::InvalidArgument, "category must not be null");
        }
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        if (_impl->packageLoadingBegun) {
            return Error(Error::InvalidArgument,
                         "categories cannot be registered after Package loading has begun");
        }
        if (!ContribLocator::isValidDottedId(category->name())) {
            return Error(Error::InvalidArgument, "category has an invalid name");
        }
        if (category->declarationMode() == ContribCategory::ModuleDeclaration &&
            category->_impl->interpreterIid.empty()) {
            return Error(Error::InvalidArgument,
                         "module category interpreter plugin IID must not be empty");
        }
        if (category->declarationMode() == ContribCategory::EntryOnly &&
            !category->_impl->interpreterIid.empty()) {
            return Error(Error::InvalidArgument,
                         "entry only category must not declare an interpreter plugin IID");
        }
        if (category->_impl->synthUnit) {
            return Error(Error::InvalidArgument, "category is already registered");
        }
        if (_impl->categories.find(category->name()) != _impl->categories.end()) {
            return Error(Error::InvalidArgument, "category name is already registered");
        }
        if (category->declarationMode() == ContribCategory::ModuleDeclaration) {
            for (const auto &item : _impl->categories) {
                if (item.second->declarationMode() == ContribCategory::ModuleDeclaration &&
                    item.second->interpreterIid() == category->interpreterIid()) {
                    return Error(Error::InvalidArgument,
                                 "interpreter IID is already registered by another category");
                }
            }
        }

        category->_impl->synthUnit = this;
        if (category->declarationMode() == ContribCategory::ModuleDeclaration) {
            _impl->pluginFactory.addStaticPlugins(category->interpreterIid());
            const auto pathsIt = _impl->pluginPaths.find(category->name());
            if (pathsIt != _impl->pluginPaths.end()) {
                _impl->pluginFactory.setPluginPaths(category->interpreterIid(), pathsIt->second);
            }
        }
        _impl->categories.emplace(category->name(), std::move(category));
        return {};
    }

    void SynthUnit::setPackagePaths(stdc::array_view<std::filesystem::path> paths) {
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        _impl->packagePaths.assign(paths.begin(), paths.end());
    }

    std::vector<std::filesystem::path> SynthUnit::packagePaths() const {
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        return _impl->packagePaths;
    }

    void SynthUnit::setPluginPaths(std::string_view category,
                                   stdc::array_view<std::filesystem::path> paths) {
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        auto &destination = _impl->pluginPaths[std::string(category)];
        destination.assign(paths.begin(), paths.end());

        const auto categoryIt = _impl->categories.find(category);
        if (categoryIt != _impl->categories.end() &&
            categoryIt->second->declarationMode() == ContribCategory::ModuleDeclaration) {
            _impl->pluginFactory.setPluginPaths(categoryIt->second->interpreterIid(), destination);
        }
    }

    std::vector<std::filesystem::path> SynthUnit::pluginPaths(std::string_view category) const {
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        const auto it = _impl->pluginPaths.find(category);
        return it == _impl->pluginPaths.end() ? std::vector<std::filesystem::path>() : it->second;
    }

    Expected<PackageHandle> SynthUnit::openPackage(const std::filesystem::path &path,
                                                   OpenMode mode) {
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        PackageLoader loader(*this);
        return loader.open(path, mode);
    }

    std::optional<PackageHandle>
        SynthUnit::findLoadedPackage(std::string_view id,
                                     const stdc::VersionNumber &version) const {
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        const auto idIt = _impl->packages.find(id);
        if (idIt == _impl->packages.end()) {
            return std::nullopt;
        }
        const auto versionIt = idIt->second.find(version);
        if (versionIt == idIt->second.end()) {
            return std::nullopt;
        }
        auto package = versionIt->second.lock();
        if (!package || !package->loaded) {
            return std::nullopt;
        }
        return PackageHandle(std::move(package));
    }

    std::vector<PackageHandle> SynthUnit::findLoadedPackages(std::string_view id) const {
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        std::vector<PackageHandle> result;
        const auto it = _impl->packages.find(id);
        if (it == _impl->packages.end()) {
            return result;
        }
        for (const auto &item : it->second) {
            if (auto package = item.second.lock(); package && package->loaded) {
                result.push_back(PackageHandle(std::move(package)));
            }
        }
        return result;
    }

    std::vector<PackageHandle> SynthUnit::loadedPackages() const {
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        std::vector<PackageHandle> result;
        for (const auto &idEntry : _impl->packages) {
            for (const auto &versionEntry : idEntry.second) {
                if (auto package = versionEntry.second.lock(); package && package->loaded) {
                    result.push_back(PackageHandle(std::move(package)));
                }
            }
        }
        return result;
    }

}
