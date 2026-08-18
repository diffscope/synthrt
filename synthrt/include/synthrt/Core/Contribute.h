#ifndef SYNTHRT_CONTRIBUTE_H
#define SYNTHRT_CONTRIBUTE_H

#include <filesystem>
#include <memory>

#include <stdcorelib/support/staticregistry.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/NamedObject.h>

namespace srt {

    class SynthUnit;

    class PackageData;

    class PackageRef;

    class ContribCategory;

    class ContribSpecHandler;

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

        /// \param handler The author's half. \sa ContribSpecHandler
        ContribSpec(std::string category, std::unique_ptr<ContribSpecHandler> handler);

        /// The handler this specification was built with, to be cast back to the type that was
        /// handed over. \sa srt_handler_t
        /// @{
        ContribSpecHandler *handlerObject();
        const ContribSpecHandler *handlerObject() const;
        /// @}

        friend class ContribCategory;
        friend class SynthUnit;
        friend class PackageData;
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
    /// Holding one of these does not register anything. \c ContribCategoryRegistry does that.
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
    /// \warning A \c SynthUnit reads the list once, when it is constructed, so a registration only
    ///          counts if it happens first. A library the program links against registers before
    ///          \c main and always makes it. A plugin does not: plugins load lazily through a
    ///          \c SynthUnit, so by the time one runs its static initializers, the unit that
    ///          loaded it has long since built its categories. Contributing a category is
    ///          something a linked library does, not something a plugin can.
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
