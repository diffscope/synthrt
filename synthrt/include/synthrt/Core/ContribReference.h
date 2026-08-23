#ifndef SYNTHRT_CONTRIBREFERENCE_H
#define SYNTHRT_CONTRIBREFERENCE_H

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/str.h>

#include <synthrt/synthrt_global.h>

namespace srt {

    /// Identifies a contribution in the current Package or one of its resolved dependencies.
    ///
    /// A ContribReference does not contain a Package version and does not perform dependency
    /// resolution. An empty package identifier refers to the current Package.
    ///
    /// \code
    /// contrib-reference = package-id ":" category "/" contribution-id
    ///                   / ":" category "/" contribution-id
    /// \endcode
    class SYNTHRT_EXPORT ContribReference {
    public:
        ContribReference() = default;

        ContribReference(std::string packageId, std::string category, std::string contributionId)
            : _packageId(std::move(packageId)), _category(std::move(category)),
              _contributionId(std::move(contributionId)) {
        }

        /// Constructs a reference to a contribution in the current Package.
        ContribReference(std::string category, std::string contributionId)
            : ContribReference({}, std::move(category), std::move(contributionId)) {
        }

        /// Returns the direct dependency Package identifier.
        ///
        /// An empty value means the current Package.
        const std::string &packageId() const {
            return _packageId;
        }

        /// Returns the contribution category.
        const std::string &category() const {
            return _category;
        }

        /// Returns the contribution identifier assigned by the Package.
        const std::string &contributionId() const {
            return _contributionId;
        }

        /// Returns whether this reference targets the current Package.
        bool isLocal() const {
            return _packageId.empty();
        }

        /// Returns whether all components satisfy the ContribReference grammar.
        bool isValid() const {
            return (_packageId.empty() || isValidPackageId(_packageId)) &&
                   isValidDottedId(_category) && isValidSegment(_contributionId);
        }

        /// Renders this value using the persistent ContribReference syntax.
        std::string toString() const {
            std::string result = _packageId;
            result += ':';
            result += _category;
            result += '/';
            result += _contributionId;
            return result;
        }

        /// Parses \a token, returning an invalid empty value when it does not match the grammar.
        static ContribReference fromString(const std::string_view token) {
            const auto colon = token.find(':');
            if (colon == std::string_view::npos ||
                token.find(':', colon + 1) != std::string_view::npos) {
                return {};
            }

            const auto packageId = token.substr(0, colon);
            const auto contribution = token.substr(colon + 1);
            const auto slash = contribution.find('/');
            if (slash == std::string_view::npos ||
                contribution.find('/', slash + 1) != std::string_view::npos) {
                return {};
            }

            const auto category = contribution.substr(0, slash);
            const auto contributionId = contribution.substr(slash + 1);
            if ((!packageId.empty() && !isValidPackageId(packageId)) ||
                !isValidDottedId(category) || !isValidSegment(contributionId)) {
                return {};
            }

            return {std::string(packageId), std::string(category), std::string(contributionId)};
        }

        /// Returns whether \a token matches the shared identifier segment grammar.
        static bool isValidSegment(const std::string_view token) {
            if (token.empty()) {
                return false;
            }
            for (const auto ch : token) {
                if (!stdc::str::is_alnum(ch) && ch != '_' && ch != '-') {
                    return false;
                }
            }
            return true;
        }

        /// Returns whether \a token is a Package identifier.
        static bool isValidPackageId(const std::string_view token) {
            return isValidSeparatedId(token, '/');
        }

        /// Returns whether \a token is a category or another dotted identifier.
        static bool isValidDottedId(const std::string_view token) {
            return isValidSeparatedId(token, '.');
        }

        bool operator==(const ContribReference &other) const {
            return _packageId == other._packageId && _category == other._category &&
                   _contributionId == other._contributionId;
        }

        bool operator!=(const ContribReference &other) const {
            return !(*this == other);
        }

    private:
        static bool isValidSeparatedId(const std::string_view token, const char separator) {
            if (token.empty()) {
                return false;
            }

            std::size_t begin = 0;
            while (true) {
                const auto end = token.find(separator, begin);
                const auto segment = end == std::string_view::npos
                                         ? token.substr(begin)
                                         : token.substr(begin, end - begin);
                if (!isValidSegment(segment)) {
                    return false;
                }
                if (end == std::string_view::npos) {
                    return true;
                }
                begin = end + 1;
            }
        }

        std::string _packageId;
        std::string _category;
        std::string _contributionId;
    };

}

#endif // SYNTHRT_CONTRIBREFERENCE_H
