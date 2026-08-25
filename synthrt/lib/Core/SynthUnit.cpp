#include "SynthUnit.h"
#include "SynthUnit_p.h"

#include <utility>

#include "ContribCategory_p.h"
#include "ContribLocator.h"
#include "Logging.h"
#include "PackageLoader_p.h"
#include "RuntimeService.h"

namespace srt {

    SynthUnit::SynthUnit() : _impl(std::make_unique<Impl>()) {
        for (const auto &entry : ContribCategoryRegistry::entries()) {
            auto category = entry.instantiate();
            if (!category) {
                logCategory().srtFatal("contribution category %1 produced no instance",
                                       entry.name());
            }
            if (category->name() != entry.name()) {
                logCategory().srtFatal(
                    "contribution category registration name %1 does not match instance name %2",
                    entry.name(), category->name());
            }
            if (auto result = addCategory(std::move(category)); !result) {
                logCategory().srtFatal("failed to register contribution category %1: %2",
                                       entry.name(), result.error().toString());
            }
        }
    }

    SynthUnit::SynthUnit(SynthUnit &&RHS) noexcept : _impl(std::move(RHS._impl)) {
        if (_impl) {
            for (auto &item : _impl->categories) {
                item.second->_impl->synthUnit = this;
            }
            for (auto &iidEntry : _impl->runtimeServices) {
                for (auto &nameEntry : iidEntry.second) {
                    nameEntry.second->m_synthUnit = this;
                }
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
            for (auto &iidEntry : _impl->runtimeServices) {
                for (auto &nameEntry : iidEntry.second) {
                    nameEntry.second->m_synthUnit = this;
                }
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

    Expected<void> SynthUnit::addRuntimeService(std::unique_ptr<RuntimeService> service) {
        if (!_impl || !service) {
            return Error(Error::InvalidArgument, "Runtime Service must not be null");
        }
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        if (_impl->packageLoadingBegun) {
            return Error(Error::InvalidArgument,
                         "Runtime Services cannot be registered after Package loading has begun");
        }
        if (!ContribLocator::isValidDottedId(service->iid())) {
            return Error(Error::InvalidArgument, "Runtime Service has an invalid IID");
        }
        if (!ContribLocator::isValidSegment(service->name())) {
            return Error(Error::InvalidArgument, "Runtime Service has an invalid name");
        }
        if (service->m_synthUnit) {
            return Error(Error::InvalidArgument, "Runtime Service is already registered");
        }

        auto &services = _impl->runtimeServices[service->iid()];
        if (services.find(service->name()) != services.end()) {
            return Error(Error::InvalidArgument, "Runtime Service identity is already registered");
        }

        auto name = service->name();
        auto registered = service.get();
        services.emplace(std::move(name), std::move(service));
        registered->m_synthUnit = this;
        return {};
    }

    RuntimeService *SynthUnit::runtimeService(std::string_view iid, std::string_view name) const {
        if (!_impl) {
            return nullptr;
        }
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        const auto iidIt = _impl->runtimeServices.find(iid);
        if (iidIt == _impl->runtimeServices.end()) {
            return nullptr;
        }
        const auto nameIt = iidIt->second.find(name);
        return nameIt == iidIt->second.end() ? nullptr : nameIt->second.get();
    }

    std::vector<RuntimeService *> SynthUnit::runtimeServices(std::string_view iid) const {
        std::vector<RuntimeService *> result;
        if (!_impl) {
            return result;
        }
        std::lock_guard<std::recursive_mutex> lock(_impl->loadMutex);
        const auto iidIt = _impl->runtimeServices.find(iid);
        if (iidIt == _impl->runtimeServices.end()) {
            return result;
        }
        result.reserve(iidIt->second.size());
        for (const auto &item : iidIt->second) {
            result.push_back(item.second.get());
        }
        return result;
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
