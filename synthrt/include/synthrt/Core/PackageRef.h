#ifndef SYNTHRT_PACKAGEREF_H
#define SYNTHRT_PACKAGEREF_H

#include <filesystem>
#include <string>
#include <utility>

#include <stdcorelib/adt/array_view.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Support/DisplayText.h>
#include <synthrt/Support/Error.h>

namespace srt {

    class SynthUnit;

    class ContribSpec;

    struct PackageDependency {
        std::string id;
        stdc::VersionNumber version;
        bool required;

        inline PackageDependency(bool required = true) : required(required) {
        }

        inline PackageDependency(std::string id, stdc::VersionNumber version, bool required = true)
            : id(std::move(id)), version(version), required(required) {
        }

        inline bool operator==(const PackageDependency &other) const {
            return id == other.id && version == other.version;
        }
    };

    class PackageData;

    class ScopedPackageRef;

    /// PackageRef - Represents a reference to a package opened by \a SynthUnit, does not own the
    /// package resources.
    class SYNTHRT_EXPORT PackageRef {
    public:
        PackageRef();
        ~PackageRef();

    public:
        inline bool isValid() const {
            return SU() != nullptr;
        }

        /// Close the package or reduce its reference count in \c SynthUnit. When all \c PackageRef
        /// instances opened using \c SynthUnit::open are closed, its shared internal data will be
        /// deleted. Anyone creating a \c PackageRef instance using a copy construct should be aware
        /// of the lifetime of the internal data.
        bool close();

        const std::string &id() const;
        stdc::VersionNumber version() const;
        stdc::VersionNumber compatVersion() const; // maybe not used

        /// Author information, for display purposes only.
        DisplayText description() const;
        DisplayText vendor() const;
        DisplayText copyright() const;
        std::filesystem::path readme() const;
        std::string_view url() const;

        /// Supported contribute categories:
        /// - \c singer:     Singer declaration
        /// - \c inference:  Inference model metadata
        std::vector<ContribSpec *> contributes(const std::string_view &category) const;
        ContribSpec *contribute(const std::string_view &category, const std::string_view &id) const;

        /// Loader-specific
        std::filesystem::path path() const;
        stdc::array_view<PackageDependency> dependencies() const;

    public:
        /// The error will be set if the package is not opened or loaded correctly.
        Error error() const;

        /// Returns true if and only if the \c noLoad option is not specified when opening the
        /// package and the loading is successful.
        ///
        /// If the package is successfully loaded, its resources are managed by \a SynthUnit, which
        /// maintains its reference count.
        bool isLoaded() const;

        /// Returns the \a SynthUnit instance that loaded this package.
        SynthUnit *SU() const;

    private:
        explicit PackageRef(PackageData *data) : _data(data) {
        }

        PackageData *_data;

        friend class ContribSpec;
        friend class SynthUnit;
        friend class ScopedPackageRef;
    };

    /// ScopedPackageRef - Represents a unique reference to a package opened by \a SynthUnit, and
    /// closes it upon destruction.
    class ScopedPackageRef : public PackageRef {
    public:
        ScopedPackageRef() = default;

        inline ScopedPackageRef(PackageRef &&RHS) : PackageRef() {
            std::swap(_data, RHS._data);
        }

        inline ~ScopedPackageRef() {
            forceClose();
        }

        inline ScopedPackageRef &operator=(PackageRef &&RHS) {
            if (this != &RHS) {
                forceClose();
                std::swap(_data, RHS._data);
            }
            return *this;
        }

        PackageRef release() {
            PackageRef ref;
            std::swap(_data, ref._data);
            return ref;
        }

    private:
        SYNTHRT_EXPORT void forceClose();

        STDCORELIB_DISABLE_COPY(ScopedPackageRef);
    };

}

#endif // SYNTHRT_PACKAGEREF_H