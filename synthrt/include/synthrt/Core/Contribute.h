#ifndef SYNTHRT_CONTRIBUTE_H
#define SYNTHRT_CONTRIBUTE_H

#include <filesystem>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/NamedObject.h>

namespace srt {

    /// Package contribution locator.
    ///
    /// Syntax:
    ///  - <package>[<version>]/<contrib>:  e.g. \c foo[1.0]/bar
    ///  - <package>/<id>:                  e.g. \c foo/bar
    ///  - <contrib>:                       e.g. \c bar
    class SYNTHRT_EXPORT ContribLocator {
    public:
        inline ContribLocator(std::string package, stdc::VersionNumber version, std::string id)
            : _package(std::move(package)), _version(std::move(version)), _id(std::move(id)) {
        }
        inline ContribLocator(std::string package, stdc::VersionNumber version)
            : _package(std::move(package)), _version(std::move(version)) {
        }
        inline ContribLocator(std::string package, std::string id)
            : _package(std::move(package)), _id(std::move(id)) {
        }
        inline ContribLocator(std::string id) : _id(std::move(id)) {
        }

        inline ContribLocator() = default;

        /// Returns the package name.
        inline const std::string &package() const {
            return _package;
        }

        /// Returns the package version.
        inline stdc::VersionNumber version() const {
            return _version;
        }

        /// Returns the contribution ID.
        inline const std::string &id() const {
            return _id;
        }

        inline bool isEmpty() const {
            return _id.empty();
        }

        std::string toString() const;

        /// Parses the contribution locator from the given string.
        static ContribLocator fromString(const std::string_view &token);

        static bool isValidLocator(const std::string_view &token);

        inline bool operator==(const ContribLocator &other) const {
            return _package == other._package && _version == other._version && _id == other._id;
        }

        inline bool operator!=(const ContribLocator &other) const {
            return !(*this == other);
        }

    protected:
        std::string _package;
        stdc::VersionNumber _version;
        std::string _id;
    };

    class SynthUnit;

    class PackageData;

    class PackageRef;

    class ContribCategory;

    class SYNTHRT_EXPORT ContribSpec {
    public:
        enum State {
            Invalid,
            Initialized,
            Ready,
            Finished,
            Deleted,
        };

        virtual ~ContribSpec();

    public:
        const std::string &category() const;
        const std::string &id() const;

    public:
        /// Load state. Internal use only.
        State state() const;
        /// Related package.
        PackageRef parent() const;
        /// Related \c SynthUnit instance.
        SynthUnit *SU() const;

    public:
        template <class T>
        inline constexpr T *as();

        template <class T>
        inline constexpr const T *as() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
        explicit ContribSpec(Impl &impl);
        explicit ContribSpec(std::string category);

        friend class ContribCategory;
        friend class SynthUnit;
    };

    template <class T>
    inline constexpr T *ContribSpec::as() {
        static_assert(std::is_base_of<ContribSpec, T>::value,
                      "T should inherit from srt::ContribSpec");
        return static_cast<T *>(this);
    }

    template <class T>
    inline constexpr const T *ContribSpec::as() const {
        static_assert(std::is_base_of<ContribSpec, T>::value,
                      "T should inherit from srt::ContribSpec");
        return static_cast<const T *>(this);
    }

    class SYNTHRT_EXPORT ContribCategory : public ObjectPool {
    public:
        ~ContribCategory();

    public:
        const std::string &name() const;

        /// Returns the related \c SynthUnit instance.
        SynthUnit *SU() const;

    public:
        template <class T>
        inline constexpr T *as();

        template <class T>
        inline constexpr const T *as() const;

    protected:
        /// Used to identify the sub-specifications of this category within the properties of
        /// \c contributes object in the manifest file.
        virtual std::string key() const = 0;

        /// Parses the contribution specification from the given JSON configuration.
        /// \param basePath The path of the configuration directory.
        /// \return The uninitialized \c ContribSpec instance.
        virtual Expected<ContribSpec *> parseSpec(const std::filesystem::path &basePath,
                                                  const JsonValue &config) const = 0;

        /// Initializes the \c ContribSpec instance in the given state.
        virtual Expected<void> loadSpec(ContribSpec *spec, ContribSpec::State state);

        std::vector<ContribSpec *> find(const ContribLocator &loc) const;

    protected:
        class Impl;
        explicit ContribCategory(Impl &impl);
        ContribCategory(std::string name, SynthUnit *su);

        friend class SynthUnit;
        friend class PackageRef;
        friend class PackageData;
    };

    template <class T>
    inline constexpr T *ContribCategory::as() {
        static_assert(std::is_base_of<ContribCategory, T>::value,
                      "T should inherit from srt::ContribCategory");
        return static_cast<T *>(this);
    }

    template <class T>
    inline constexpr const T *ContribCategory::as() const {
        static_assert(std::is_base_of<ContribCategory, T>::value,
                      "T should inherit from srt::ContribCategory");
        return static_cast<const T *>(this);
    }

    template <class T>
    class ContribCategoryRegistrar {
        static_assert(std::is_base_of<ContribCategory, T>::value,
                      "T should inherit from srt::ContribCategory");

    public:
        inline ContribCategoryRegistrar(ContribCategory *(*fac)(SynthUnit *) ) {
            SynthUnit::registerCategoryFactory(fac);
        }

        inline ContribCategoryRegistrar() {
            SynthUnit::registerCategoryFactory([](SynthUnit *su) -> ContribCategory * {
                return new T(su); //
            });
        }
    };

}

#endif // SYNTHRT_CONTRIBUTE_H