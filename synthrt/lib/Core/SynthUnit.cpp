#include "SynthUnit.h"
#include "SynthUnit_p.h"

#include <utility>

#include "ContribCategory_p.h"
#include "ContribReference.h"

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
                    versionEntry.second->synthUnit = this;
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
                    versionEntry.second->synthUnit = this;
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
        if (_impl->packageLoadingBegun) {
            return Error(Error::InvalidArgument,
                         "categories cannot be registered after Package loading has begun");
        }
        if (!ContribReference::isValidDottedId(category->name())) {
            return Error(Error::InvalidArgument, "category has an invalid name");
        }
        if (category->_impl->synthUnit) {
            return Error(Error::InvalidArgument, "category is already registered");
        }
        if (_impl->categories.find(category->name()) != _impl->categories.end()) {
            return Error(Error::InvalidArgument, "category name is already registered");
        }

        category->_impl->synthUnit = this;
        _impl->categories.emplace(category->name(), std::move(category));
        return {};
    }

    void SynthUnit::setPackagePaths(stdc::array_view<std::filesystem::path> paths) {
        _impl->packagePaths.assign(paths.begin(), paths.end());
    }

    std::vector<std::filesystem::path> SynthUnit::packagePaths() const {
        return _impl->packagePaths;
    }

    void SynthUnit::setPluginPaths(std::string_view category,
                                   stdc::array_view<std::filesystem::path> paths) {
        auto &destination = _impl->pluginPaths[std::string(category)];
        destination.assign(paths.begin(), paths.end());
    }

    std::vector<std::filesystem::path> SynthUnit::pluginPaths(std::string_view category) const {
        const auto it = _impl->pluginPaths.find(category);
        return it == _impl->pluginPaths.end() ? std::vector<std::filesystem::path>() : it->second;
    }

    Expected<PackageHandle> SynthUnit::openPackage(const std::filesystem::path &, OpenMode) {
        _impl->packageLoadingBegun = true;
        return Error(Error::NotImplemented,
                     "Package loading is not implemented by the current Core migration layer");
    }

    std::optional<PackageHandle>
        SynthUnit::findLoadedPackage(std::string_view id,
                                     const stdc::VersionNumber &version) const {
        const auto idIt = _impl->packages.find(id);
        if (idIt == _impl->packages.end()) {
            return std::nullopt;
        }
        const auto versionIt = idIt->second.find(version);
        if (versionIt == idIt->second.end() || !versionIt->second->loaded) {
            return std::nullopt;
        }
        return PackageHandle(versionIt->second);
    }

    std::vector<PackageHandle> SynthUnit::findLoadedPackages(std::string_view id) const {
        std::vector<PackageHandle> result;
        const auto it = _impl->packages.find(id);
        if (it == _impl->packages.end()) {
            return result;
        }
        for (const auto &item : it->second) {
            if (item.second->loaded) {
                result.push_back(PackageHandle(item.second));
            }
        }
        return result;
    }

    std::vector<PackageHandle> SynthUnit::loadedPackages() const {
        std::vector<PackageHandle> result;
        for (const auto &idEntry : _impl->packages) {
            for (const auto &versionEntry : idEntry.second) {
                if (versionEntry.second->loaded) {
                    result.push_back(PackageHandle(versionEntry.second));
                }
            }
        }
        return result;
    }

}
