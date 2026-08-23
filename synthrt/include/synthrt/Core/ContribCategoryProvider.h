#ifndef SYNTHRT_CONTRIBCATEGORYPROVIDER_H
#define SYNTHRT_CONTRIBCATEGORYPROVIDER_H

#include <memory>
#include <string_view>

#include <synthrt/synthrt_global.h>

namespace srt {

    class ContribCategory;
    class SynthUnit;

    /// Provides one contribution category during SynthUnit initialization.
    ///
    /// An extension library implements this interface and gives an instance to
    /// \c SynthUnit::addCategoryProvider() before the first Package is opened.
    class SYNTHRT_EXPORT ContribCategoryProvider {
    public:
        virtual ~ContribCategoryProvider();

        /// Returns the unique category name provided by this extension.
        virtual std::string_view categoryName() const noexcept = 0;

        /// Creates the category object owned by \a synthUnit.
        virtual std::unique_ptr<ContribCategory> createCategory(SynthUnit &synthUnit) const = 0;

    protected:
        ContribCategoryProvider() = default;

    private:
        ContribCategoryProvider(const ContribCategoryProvider &) = delete;
        ContribCategoryProvider &operator=(const ContribCategoryProvider &) = delete;
    };

}

#endif // SYNTHRT_CONTRIBCATEGORYPROVIDER_H
