#ifndef SYNTHRT_SYNTHUNIT_H
#define SYNTHRT_SYNTHUNIT_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <stdcorelib/adt/array_view.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribExecutive.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/RuntimeService.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    class PackageData;
    class PackageLoader;

    /// Owns Package resolution, interpreter discovery, and committed runtime state.
    class SYNTHRT_EXPORT SynthUnit {
    public:
        /// Constructs a unit with every category in \c ContribCategoryRegistry.
        SynthUnit();
        SynthUnit(SynthUnit &&RHS) noexcept;
        SynthUnit &operator=(SynthUnit &&RHS) noexcept;

        /// \warning All PackageHandle objects owned by this unit must have been released before
        ///          destruction. All borrowed contribution pointers must no longer be used.
        ~SynthUnit();

    public:
        /// Selects how far Package opening may progress.
        enum OpenMode {
            DataOnly, ///< Parses manifests as data without discovering or loading providers.
            Load,     ///< Validates, loads, and commits the Package and its dependencies.
        };

        ContribCategory *category(std::string_view name);
        const ContribCategory *category(std::string_view name) const;

        /// Registers and takes ownership of one category before the first Package is opened.
        ///
        /// Registration binds the category to this SynthUnit. It fails when the name is already
        /// registered or Package loading has begun.
        Expected<void> addCategory(std::unique_ptr<ContribCategory> category);

        /// Registers and takes ownership of one Runtime Service before Package loading begins.
        ///
        /// The pair formed by the service IID and name must be unique within this SynthUnit.
        Expected<void> addRuntimeService(std::unique_ptr<RuntimeService> service);

        /// Finds a Runtime Service by interface and implementation name.
        RuntimeService *runtimeService(std::string_view iid, std::string_view name) const;

        /// Returns all Runtime Services registered for one interface.
        std::vector<RuntimeService *> runtimeServices(std::string_view iid) const;

        /// Replaces the Package search path sequence.
        void setPackagePaths(stdc::array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> packagePaths() const;

        /// Replaces the interpreter plugin search path sequence for \a category.
        void setPluginPaths(std::string_view category,
                            stdc::array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> pluginPaths(std::string_view category) const;

        /// Opens a Package using \a mode.
        ///
        /// \c DataOnly reads manifests without discovering interpreters or changing committed
        /// state.
        /// \c Load begins with an internal Probe, resolves dependencies, validates contributions,
        /// and returns only after an infallible commit. A failure is returned as an Error and
        /// never as a partially valid PackageHandle.
        Expected<PackageHandle> openPackage(const std::filesystem::path &path, OpenMode mode);

        /// Finds a committed Package with the exact identifier and version.
        std::optional<PackageHandle> findLoadedPackage(std::string_view id,
                                                       const stdc::VersionNumber &version) const;

        /// Returns committed Packages with the given identifier.
        std::vector<PackageHandle> findLoadedPackages(std::string_view id) const;

        /// Returns all committed Packages.
        std::vector<PackageHandle> loadedPackages() const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class ContribCategory;
        friend class ContribExecutive;
        friend class PackageHandle;
        friend class PackageData;
        friend class PackageLoader;
        friend class RuntimeService;
    };

}

#endif // SYNTHRT_SYNTHUNIT_H
