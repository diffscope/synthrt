#ifndef SYNTHRT_CONTRIBCATEGORY_H
#define SYNTHRT_CONTRIBCATEGORY_H

#include <filesystem>
#include <vector>

#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

/// \file
/// The contributes of one kind, as the rest of the program sees them.
///
/// This is the user's half. What it takes to define a new kind is in ContribHandler.h.

namespace srt {

    class SynthUnit;

    class PackageData;

    class ContribHandler;

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

    public:
        /// The contributes of this kind that match \a loc.
        std::vector<ContribSpec *> find(const ContribLocator &loc) const;

    protected:
        class Impl;

        /// \param handler The handler that built this category and owns it.
        explicit ContribCategory(ContribHandler *handler);

        /// For a category defined inside synthrt, which can see this Impl.
        explicit ContribCategory(Impl &impl);

        friend class SynthUnit;
        friend class PackageRef;
        friend class PackageData;
        friend class ContribHandler;
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

}

#endif // SYNTHRT_CONTRIBCATEGORY_H
