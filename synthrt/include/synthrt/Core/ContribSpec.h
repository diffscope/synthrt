#ifndef SYNTHRT_CONTRIBSPEC_H
#define SYNTHRT_CONTRIBSPEC_H

#include <memory>
#include <string>
#include <type_traits>

#include <synthrt/synthrt_global.h>

/// \file
/// One contribute a package declares, as the rest of the program sees it.
///
/// This is the user's half. What it takes to define a new kind of contribute is in
/// ContribHandler.h.

namespace srt {

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

        /// Builds one carrying no state beyond the category it belongs to.
        ///
        /// Whatever a kind reads out of a manifest is its own business: put it in the derived
        /// class, or behind a pointer of its own, or wherever suits. Nothing here has to be
        /// inherited to make that work.
        explicit ContribSpec(std::string category);

        /// For a specification defined inside synthrt, which can see this Impl.
        explicit ContribSpec(Impl &impl);

        friend class ContribCategory;
        friend class SynthUnit;
        friend class PackageData;
        friend class ContribHandler;
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

}

#endif // SYNTHRT_CONTRIBSPEC_H
