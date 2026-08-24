#ifndef DSINFER_INFERENCEDRIVERFACTORY_H
#define DSINFER_INFERENCEDRIVERFACTORY_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/adt/array_view.h>
#include <stdcorelib/plugin/pluginfactory.h>
#include <synthrt/Support/Expected.h>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/dsinfer_global.h>

namespace ds {

    /// Discovers inference driver plugins and creates their Runtime Services.
    ///
    /// Loaded plugin code is retained by this factory. Every driver created from it must be
    /// destroyed before the factory.
    class DSINFER_EXPORT InferenceDriverFactory : public stdc::plugin::BundlePluginFactory {
    public:
        InferenceDriverFactory();
        ~InferenceDriverFactory();

        InferenceDriverFactory(InferenceDriverFactory &&RHS) noexcept;
        InferenceDriverFactory &operator=(InferenceDriverFactory &&RHS) noexcept;

        /// Adds a directory containing inference driver plugins.
        void addPluginPath(const std::filesystem::path &path);

        /// Replaces the inference driver plugin search path sequence.
        void setPluginPaths(stdc::array_view<std::filesystem::path> paths);

        /// Returns the inference driver plugin search path sequence.
        std::vector<std::filesystem::path> pluginPaths() const;

        /// Returns valid plugin neutral driver names in discovery order.
        std::vector<std::string> driverNames() const;

        /// Loads the first plugin named \a name and creates its driver.
        srt::Expected<std::unique_ptr<InferenceDriver>> create(std::string_view name);
    };

}

#endif // DSINFER_INFERENCEDRIVERFACTORY_H
