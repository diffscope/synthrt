#include "InferenceDriverFactory.h"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

#include <synthrt/Core/ContribLocator.h>

#include "InferenceDriverPlugin.h"

namespace ds {

    namespace {

        std::optional<std::string> driverBackend(const stdc::json::Value &metadata) {
            if (!metadata.isObject()) {
                return std::nullopt;
            }
            const auto &name = metadata["name"];
            if (!name.isString() || !srt::ContribLocator::isValidSegment(name.toString())) {
                return std::nullopt;
            }
            const auto &backend = metadata["backend"];
            if (!backend.isString() || !srt::ContribLocator::isValidDottedId(backend.toString())) {
                return std::nullopt;
            }
            return backend.toString();
        }

    }

    InferenceDriverFactory::InferenceDriverFactory() {
        addStaticPlugins(InferenceDriverPlugin::IID);
    }

    InferenceDriverFactory::~InferenceDriverFactory() = default;

    InferenceDriverFactory::InferenceDriverFactory(InferenceDriverFactory &&RHS) noexcept = default;

    InferenceDriverFactory &
        InferenceDriverFactory::operator=(InferenceDriverFactory &&RHS) noexcept = default;

    void InferenceDriverFactory::addPluginPath(const std::filesystem::path &path) {
        PluginFactory::addPluginPath(InferenceDriverPlugin::IID, path);
    }

    void InferenceDriverFactory::setPluginPaths(stdc::array_view<std::filesystem::path> paths) {
        PluginFactory::setPluginPaths(InferenceDriverPlugin::IID, paths);
    }

    std::vector<std::filesystem::path> InferenceDriverFactory::pluginPaths() const {
        return PluginFactory::pluginPaths(InferenceDriverPlugin::IID);
    }

    std::vector<std::string> InferenceDriverFactory::backends() const {
        std::vector<std::string> result;
        std::set<std::string> visited;
        for (auto loader : plugins(InferenceDriverPlugin::IID)) {
            auto backend = driverBackend(loader->metadata());
            if (backend && visited.insert(*backend).second) {
                result.push_back(std::move(*backend));
            }
        }
        return result;
    }

    stdc::plugin::PluginLoader *InferenceDriverFactory::find(std::string_view backend) const {
        if (!srt::ContribLocator::isValidDottedId(backend)) {
            return nullptr;
        }
        for (auto loader : plugins(InferenceDriverPlugin::IID)) {
            const auto candidateBackend = driverBackend(loader->metadata());
            if (candidateBackend && *candidateBackend == backend) {
                return loader;
            }
        }
        return nullptr;
    }

    srt::Expected<std::unique_ptr<InferenceDriver>>
        InferenceDriverFactory::create(stdc::plugin::PluginLoader *loader) {
        if (!loader) {
            return srt::Error(srt::Error::InvalidArgument,
                              "inference driver plugin loader must not be null");
        }
        const auto driverPlugins = plugins(InferenceDriverPlugin::IID);
        if (std::find(driverPlugins.begin(), driverPlugins.end(), loader) == driverPlugins.end()) {
            return srt::Error(srt::Error::InvalidArgument,
                              "inference driver plugin loader does not belong to this factory");
        }
        if (loader->iid() != InferenceDriverPlugin::IID) {
            return srt::Error(srt::Error::InvalidArgument,
                              "plugin loader does not provide an inference driver");
        }
        const auto backend = driverBackend(loader->metadata());
        if (!backend) {
            return srt::Error(srt::Error::InvalidFormat,
                              "inference driver plugin metadata is invalid");
        }
        if (!loader->load()) {
            return srt::Error(srt::Error::FileNotOpen,
                              "failed to load inference driver plugin: " + loader->errorMessage());
        }

        auto plugin = static_cast<InferenceDriverPlugin *>(loader->plugin());
        auto result = plugin->create();
        if (!result) {
            return result.takeError().withContext("failed to create inference driver");
        }
        auto driver = result.take();
        if (!driver) {
            return srt::Error(srt::Error::InvalidFormat,
                              "inference driver plugin returned a null driver");
        }
        if (driver->iid() != InferenceDriverPlugin::IID || driver->backend() != *backend) {
            return srt::Error(srt::Error::InvalidFormat,
                              "inference driver identity does not match plugin metadata");
        }
        return driver;
    }

}
