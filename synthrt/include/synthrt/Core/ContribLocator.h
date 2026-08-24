#ifndef SYNTHRT_CONTRIBLOCATOR_H
#define SYNTHRT_CONTRIBLOCATOR_H

#include <string>
#include <string_view>
#include <utility>

#include <synthrt/synthrt_global.h>

namespace srt {

    /// Identifies a contribution in the current Package or one of its resolved dependencies.
    ///
    /// A ContribLocator does not contain a Package version and does not perform dependency
    /// resolution. An empty package identifier refers to the current Package.
    ///
    /// \code
    /// contrib-locator = package-id ":" category "/" contribution-id
    ///                 / ":" category "/" contribution-id
    /// \endcode
    class SYNTHRT_EXPORT ContribLocator {
    public:
        ContribLocator() = default;

        ContribLocator(std::string packageId, std::string category, std::string contributionId)
            : m_packageId(std::move(packageId)), m_category(std::move(category)),
              m_contributionId(std::move(contributionId)) {
        }

        /// Constructs a locator for a contribution in the current Package.
        ContribLocator(std::string category, std::string contributionId)
            : ContribLocator({}, std::move(category), std::move(contributionId)) {
        }

        /// Returns the direct dependency Package identifier.
        ///
        /// An empty value means the current Package.
        const std::string &packageId() const {
            return m_packageId;
        }

        /// Returns the contribution category.
        const std::string &category() const {
            return m_category;
        }

        /// Returns the contribution identifier assigned by the Package.
        const std::string &contributionId() const {
            return m_contributionId;
        }

        /// Returns whether this locator targets the current Package.
        bool isLocal() const {
            return m_packageId.empty();
        }

        /// Returns whether all components satisfy the ContribLocator grammar.
        bool isValid() const {
            return (m_packageId.empty() || isValidPackageId(m_packageId)) &&
                   isValidDottedId(m_category) && isValidSegment(m_contributionId);
        }

        /// Renders this value using the persistent ContribLocator syntax.
        std::string toString() const;

        /// Parses \a token, returning an invalid empty value when it does not match the grammar.
        static ContribLocator fromString(std::string_view token);

        /// Returns whether \a token matches the shared identifier segment grammar.
        static bool isValidSegment(std::string_view token);

        /// Returns whether \a token is a Package identifier.
        static bool isValidPackageId(std::string_view token);

        /// Returns whether \a token is a category or another dotted identifier.
        static bool isValidDottedId(std::string_view token);

        bool operator==(const ContribLocator &other) const {
            return m_packageId == other.m_packageId && m_category == other.m_category &&
                   m_contributionId == other.m_contributionId;
        }

        bool operator!=(const ContribLocator &other) const {
            return !(*this == other);
        }

    private:
        static bool isValidSeparatedId(std::string_view token, char separator);

        std::string m_packageId;
        std::string m_category;
        std::string m_contributionId;
    };

}

#endif // SYNTHRT_CONTRIBLOCATOR_H
