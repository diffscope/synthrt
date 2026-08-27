#include "InferenceDriverFactory.h"

#include <algorithm>
#include <optional>
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

    InferenceDriverFactory::InferenceDriverFactory()
        : PluginCatalog(InferenceDriverPlugin::IID,
                        std::make_unique<stdc::plugin::BundlePluginFactory>()) {
        factory()->addStaticPlugins(iid());
    }

    InferenceDriverFactory::~InferenceDriverFactory() = default;

    InferenceDriverFactory::InferenceDriverFactory(InferenceDriverFactory &&RHS) noexcept = default;

    InferenceDriverFactory &
        InferenceDriverFactory::operator=(InferenceDriverFactory &&RHS) noexcept = default;

    srt::Expected<std::unique_ptr<InferenceDriver>>
        InferenceDriverFactory::create(stdc::plugin::PluginLoader *loader) {
        if (!loader) {
            return srt::Error(srt::Error::InvalidArgument,
                              "inference driver plugin loader must not be null");
        }
        const auto driverPlugins = loaders();
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

    std::vector<std::string>
        InferenceDriverFactory::keysFromMetadata(const stdc::json::Value &metadata) const {
        auto backend = driverBackend(metadata);
        if (!backend) {
            return {};
        }
        std::vector<std::string> result;
        result.push_back(std::move(*backend));
        return result;
    }

}
