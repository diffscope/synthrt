#ifndef SYNTHRT_SYNTHUNIT_H
#define SYNTHRT_SYNTHUNIT_H

#include <filesystem>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Plugin/PluginFactory.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    class PackageRef;

    class ContribCategory;

    template <class T>
    class ContribCategoryRegistrar;

    /// SynthUnit is the main class for loading and managing SynthRT packages.
    class SYNTHRT_EXPORT SynthUnit : public PluginFactory {
    public:
        SynthUnit();
        ~SynthUnit();

        ContribCategory *category(const std::string_view &name) const;

    public:
        /// Configure the package searching paths.
        inline void addPackagePath(const std::filesystem::path &path);
        void addPackagePaths(stdc::array_view<std::filesystem::path> paths);
        void setPackagePaths(stdc::array_view<std::filesystem::path> paths);

        std::vector<std::filesystem::path> packagePaths() const;

    public:
        /// Opens a package and returns a reference to it.
        ///
        /// If opened in load mode, dependencies will be loaded by searching in the configured
        /// package paths in sequence. Returns \c true only if all dependencies are successfully
        /// loaded with no circular dependencies. Both direct loads and dependency loads via this
        /// interface increase the package's reference count.
        ///
        /// \param path   The directory to the package to open.
        /// \param noLoad Whether to only read the metadata (true) or open in load mode (false).
        Expected<PackageRef> open(const std::filesystem::path &path, bool noLoad);

        /// Find a loaded package by ID and version.
        PackageRef find(const std::string_view &id, const stdc::VersionNumber &version) const;

        /// Find all loaded packages with the given ID.
        std::vector<PackageRef> find(const std::string_view &id) const;

        /// Returns all loaded packages.
        std::vector<PackageRef> packages() const;

    protected:
        class Impl;

        static void registerCategoryFactory(ContribCategory *(*fac)(SynthUnit *));

        friend class PackageRef;
        friend class ContribCategory;

        template <class T>
        friend class ContribCategoryRegistrar;
    };

    inline void SynthUnit::addPackagePath(const std::filesystem::path &path) {
        addPackagePaths({path});
    }

}

#endif // SYNTHRT_SYNTHUNIT_H