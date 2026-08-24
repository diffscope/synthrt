#include "InferenceDriverFactory.h"

#include <optional>
#include <set>
#include <utility>

#include <synthrt/Core/ContribLocator.h>

#include "InferenceDriverPlugin.h"

namespace ds {

    namespace {

        std::optional<std::string> driverName(const stdc::json::Value &manifest) {
            if (!manifest.isObject()) {
                return std::nullopt;
            }
            const auto &name = manifest["name"];
            if (!name.isString() || !srt::ContribLocator::isValidSegment(name.toString())) {
                return std::nullopt;
            }
            return name.toString();
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

    std::vector<std::string> InferenceDriverFactory::driverNames() const {
        std::vector<std::string> result;
        std::set<std::string> visited;
        for (auto *loader : plugins(InferenceDriverPlugin::IID)) {
            auto name = driverName(loader->manifest());
            if (name && visited.insert(*name).second) {
                result.push_back(std::move(*name));
            }
        }
        return result;
    }

    srt::Expected<std::unique_ptr<InferenceDriver>>
        InferenceDriverFactory::create(std::string_view name) {
        if (!srt::ContribLocator::isValidSegment(name)) {
            return srt::Error(srt::Error::InvalidArgument, "driver name is invalid");
        }

        for (auto *loader : plugins(InferenceDriverPlugin::IID)) {
            const auto candidateName = driverName(loader->manifest());
            if (!candidateName || *candidateName != name) {
                continue;
            }
            if (!loader->load()) {
                return srt::Error(srt::Error::FileNotOpen,
                                  "failed to load inference driver plugin: " +
                                      loader->errorMessage());
            }

            auto *plugin = static_cast<InferenceDriverPlugin *>(loader->plugin());
            auto result = plugin->create();
            if (!result) {
                return result.takeError().withContext("failed to create inference driver");
            }
            auto driver = result.take();
            if (!driver) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "inference driver plugin returned a null driver");
            }
            if (driver->iid() != InferenceDriver::IID || driver->name() != name) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "inference driver identity does not match plugin metadata");
            }
            return driver;
        }

        return srt::Error(srt::Error::FileNotFound,
                          "inference driver plugin was not found: " + std::string(name));
    }

}
