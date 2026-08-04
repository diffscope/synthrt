#ifndef SYNTHRT_CONTRIBUTE_H
#define SYNTHRT_CONTRIBUTE_H

#include <filesystem>
#include <memory>

#include <stdcorelib/support/staticregistry.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/NamedObject.h>

namespace srt {

    /// Reference to a package, or to one contribute inside a package.
    ///
    /// Syntax:
    /// \code
    ///     reference     = package-part [ ":" contrib-part ]
    ///                   / ":" contrib-part
    ///     package-part  = package-id [ "=" version ]
    ///     contrib-part  = [ category "/" ] contrib-id
    ///
    ///     package-id    = segment *( "/" segment )
    ///     category      = segment
    ///     contrib-id    = segment
    ///     segment       = 1*( ALPHA / DIGIT / "_" / "-" )
    /// \endcode
    ///
    /// \code
    ///     vendor/sample=1.0.0.0:inference/acoustic    // fully qualified
    ///     vendor/sample=1.0.0.0:singer/main
    ///     vendor/sample=1.0.0.0:acoustic              // category left to resolution
    ///     vendor/sample:singer/main                   // version left to resolution
    ///     :singer/main                                // the current package
    ///     vendor/sample=1.0.0.0                       // the package itself
    /// \endcode
    ///
    /// The leading \c ":" is not optional when no package part is given: \c singer/main on its own
    /// is a reference to the package of that name, not to the contribute \c main of category
    /// \c singer.
    ///
    /// The category carries the contribute kind as data rather than as punctuation, so a new kind
    /// of contribute needs no change to this grammar. It may be omitted, in which case resolution
    /// searches every category and reports an ambiguity if more than one matches - which can
    /// happen, as a singer and an inference of the same name may coexist in one package.
    class SYNTHRT_EXPORT ContribLocator {
    public:
        inline ContribLocator() = default;

        inline ContribLocator(std::string package, stdc::VersionNumber version,
                              std::string category, std::string id)
            : _package(std::move(package)), _version(std::move(version)),
              _category(std::move(category)), _id(std::move(id)) {
        }

        inline ContribLocator(std::string package, stdc::VersionNumber version)
            : _package(std::move(package)), _version(std::move(version)) {
        }

        /// Returns the package identifier, empty when the reference names no package.
        inline const std::string &package() const {
            return _package;
        }

        /// Returns the package version, empty when the reference pins none.
        inline stdc::VersionNumber version() const {
            return _version;
        }

        /// Returns the contribute category, empty when the reference leaves it to resolution.
        inline const std::string &category() const {
            return _category;
        }

        /// Returns the contribute identifier, empty when the reference names a package only.
        inline const std::string &id() const {
            return _id;
        }

        /// Returns whether this reference names nothing at all.
        inline bool isEmpty() const {
            return _package.empty() && _id.empty();
        }

        /// Renders the reference, which parses back to an equal value.
        std::string toString() const;

        /// Parses a reference, returning an empty locator when \a token does not match the
        /// grammar.
        static ContribLocator fromString(const std::string_view &token);

        /// Returns whether \a token is a \c segment, the shape every category and contribute
        /// identifier takes.
        static bool isValidSegment(const std::string_view &token);

        /// Returns whether \a token is a \c package-id, one or more segments joined by \c "/".
        static bool isValidPackageId(const std::string_view &token);

        inline bool operator==(const ContribLocator &other) const {
            return _package == other._package && _version == other._version &&
                   _category == other._category && _id == other._id;
        }

        inline bool operator!=(const ContribLocator &other) const {
            return !(*this == other);
        }

    protected:
        std::string _package;
        stdc::VersionNumber _version;
        std::string _category;
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
        /// The category's identifier, serving both roles it has: the property naming this
        /// category's list inside a manifest's \c contributes object, and the segment sitting
        /// between the \c ":" and the \c "/" of a reference.
        ///
        /// It must therefore be a \c segment, which the constructor asserts.
        const std::string &name() const;

        /// Returns the related \c SynthUnit instance.
        SynthUnit *SU() const;

    public:
        template <class T>
        inline constexpr T *as();

        template <class T>
        inline constexpr const T *as() const;

    protected:
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

    /// Builds one category, bound to the \c SynthUnit asking for it.
    class ContribCategoryFactoryBase {
    public:
        virtual ~ContribCategoryFactoryBase() = default;

        virtual std::unique_ptr<ContribCategory> create(SynthUnit *su) const = 0;
    };

    /// \a T 's own factory, which \a T befriends so that its constructor can stay protected.
    ///
    /// Holding one of these does not register anything. The registry that turns a factory into a
    /// category every \c SynthUnit builds is internal to synthrt, whose set of categories is its
    /// own and closed.
    template <class T>
    class ContribCategoryFactory : public ContribCategoryFactoryBase {
        static_assert(std::is_base_of<ContribCategory, T>::value,
                      "T should inherit from srt::ContribCategory");

    public:
        /// \note Not \c std::make_unique, which is nobody's friend and so cannot reach \a T 's
        ///       protected constructor.
        std::unique_ptr<ContribCategory> create(SynthUnit *su) const override {
            return std::unique_ptr<ContribCategory>(new T(su));
        }
    };

    /// The registered categories, one entry per kind.
    ///
    /// Registering a factory rather than the category type itself, because a registry entry
    /// constructs its subject with no arguments while a category needs its unit.
    ///
    /// A category registers itself by declaring an \c Add at namespace scope:
    ///
    /// \code
    ///     static ContribCategoryRegistry::Add<ContribCategoryFactory<SingerCategory>>
    ///         registrar("singer", "Singer contributes");
    /// \endcode
    ///
    /// The name has to match what the category hands its base constructor, which \c SynthUnit
    /// checks as it builds them.
    ///
    /// Reading this is how to ask which kinds of contribute a build understands. Registering into
    /// it from outside synthrt does not do anything useful: a \c SynthUnit reads the list once, as
    /// it is constructed, and a plugin is loaded through a unit that has already done so. The set
    /// is synthrt's own. What the registration buys internally is that \c Core never has to name
    /// the categories \c SVS defines.
    using ContribCategoryRegistry = stdc::StaticRegistry<ContribCategoryFactoryBase>;

}

/// Keeps a caller outside synthrt from instantiating a second, empty list of its own. Without it
/// the registry reads as if nothing had ever registered, and on Windows does not even link.
///
/// Only for that caller. Inside the library this would be an instantiation declaration carrying
/// \c dllexport, which is not what that means, and the definition lives in Contribute.cpp anyway.
#ifndef SYNTHRT_LIBRARY
extern template class SYNTHRT_EXPORT stdc::StaticRegistry<srt::ContribCategoryFactoryBase>;
#endif

#endif // SYNTHRT_CONTRIBUTE_H
