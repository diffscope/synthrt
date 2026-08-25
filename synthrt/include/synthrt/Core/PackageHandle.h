#ifndef SYNTHRT_PACKAGEHANDLE_H
#define SYNTHRT_PACKAGEHANDLE_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/adt/array_view.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Support/DisplayText.h>
#include <synthrt/Support/JSON.h>

namespace srt {

    class ContribSpec;
    class PackageData;
    class PackageLoader;

    class SynthUnit;

    /// Describes one required direct Package dependency.
    struct PackageDependency {
        inline PackageDependency() = default;

        inline PackageDependency(std::string id, stdc::VersionNumber version)
            : id(std::move(id)), version(std::move(version)) {
        }

        inline bool operator==(const PackageDependency &other) const {
            return id == other.id && version == other.version;
        }

        inline bool operator!=(const PackageDependency &other) const {
            return !(*this == other);
        }

    public:
        std::string id;
        stdc::VersionNumber version;
    };

    /// A shared handle to Package metadata and any committed runtime resources it owns.
    ///
    /// Copying a PackageHandle retains the Package. Destroying or resetting the last handle
    /// releases it. Every handle must be destroyed or reset before its SynthUnit is destroyed.
    class SYNTHRT_EXPORT PackageHandle {
    public:
        PackageHandle() = default;
        PackageHandle(const PackageHandle &other);
        PackageHandle(PackageHandle &&other) noexcept;
        PackageHandle &operator=(const PackageHandle &other);
        PackageHandle &operator=(PackageHandle &&other) noexcept;
        ~PackageHandle();

        /// Returns whether this object refers to a Package.
        explicit operator bool() const noexcept;

        /// Releases this handle and makes it empty.
        void reset() noexcept;

        const std::string &id() const;
        stdc::VersionNumber version() const;
        stdc::VersionNumber compatVersion() const;
        int runtimeLevel() const;

        /// Returns the expanded object read from \c desc.json, including unknown fields.
        const JsonObject &manifestDeclaration() const;

        /// Returns whether this Package completed loading and was committed.
        bool isLoaded() const;

        /// Returns the SynthUnit that owns this Package.
        SynthUnit &synthUnit() const;

        /// Author information for display purposes only.
        DisplayText name() const;
        DisplayText description() const;
        DisplayText vendor() const;
        DisplayText readme() const;
        DisplayText copyright() const;
        const std::string &url() const;

        /// Returns the contributions declared under \a category.
        ///
        /// The returned pointers remain valid while a handle retains the corresponding Package.
        std::vector<ContribSpec *> contributions(std::string_view category) const;

        /// Finds a contribution by category and Package local identifier.
        ///
        /// Returns \c nullptr when this Package does not contain the requested contribution. The
        /// returned pointer remains valid while a handle retains the corresponding Package.
        ContribSpec *contribution(std::string_view category, std::string_view id) const;

        /// Resolves \a locator against this Package and its bound direct dependencies.
        ///
        /// Resolution never searches for or loads another Package version. Returns \c nullptr when
        /// the locator does not identify a contribution in the resolved dependency context.
        ContribSpec *resolve(const ContribLocator &locator) const;

        const std::filesystem::path &path() const;
        stdc::array_view<PackageDependency> dependencies() const;

        bool operator==(const PackageHandle &other) const noexcept;
        bool operator!=(const PackageHandle &other) const noexcept;

    private:
        explicit PackageHandle(std::shared_ptr<PackageData> data);

        std::shared_ptr<PackageData> m_data;

        friend class ContribSpec;
        friend class PackageLoader;
        friend class SynthUnit;
    };

}

#endif // SYNTHRT_PACKAGEHANDLE_H
