#include "ContribLocator.h"

#include <cstddef>

#include <stdcorelib/str.h>

namespace srt {

    std::string ContribLocator::toString() const {
        std::string result = m_packageId;
        result += ':';
        result += m_category;
        result += '/';
        result += m_contributionId;
        return result;
    }

    ContribLocator ContribLocator::fromString(std::string_view token) {
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
        if ((!packageId.empty() && !isValidPackageId(packageId)) || !isValidDottedId(category) ||
            !isValidSegment(contributionId)) {
            return {};
        }

        return {std::string(packageId), std::string(category), std::string(contributionId)};
    }

    bool ContribLocator::isValidSegment(std::string_view token) {
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

    bool ContribLocator::isValidPackageId(std::string_view token) {
        return isValidSeparatedId(token, '/');
    }

    bool ContribLocator::isValidDottedId(std::string_view token) {
        return isValidSeparatedId(token, '.');
    }

    bool ContribLocator::isValidSeparatedId(std::string_view token, char separator) {
        if (token.empty()) {
            return false;
        }

        std::size_t begin = 0;
        while (true) {
            const auto end = token.find(separator, begin);
            const auto segment = end == std::string_view::npos ? token.substr(begin)
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

}
