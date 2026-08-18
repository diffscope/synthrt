#include "ContribLocator.h"

#include <stdcorelib/str.h>

namespace srt {

    /// Returns whether \a token matches the grammar's \c version production, being up to four
    /// dot-separated runs of digits with no leading zero.
    ///
    /// \c stdc::VersionNumber::fromString() is not a substitute. It accepts trailing rubbish and
    /// extra segments, so a reference such as <tt>pkg=1.2.3.4.5</tt> would otherwise parse.
    static bool isValidVersion(const std::string_view &token) {
        size_t begin = 0;
        int segments = 0;
        while (true) {
            const auto dot = token.find('.', begin);
            const auto segment = dot == std::string_view::npos ? token.substr(begin)
                                                               : token.substr(begin, dot - begin);
            if (segment.empty() || ++segments > 4) {
                return false;
            }
            if (segment.size() > 1 && segment.front() == '0') {
                return false;
            }
            for (const auto ch : segment) {
                if (!stdc::str::is_digit(ch)) {
                    return false;
                }
            }
            if (dot == std::string_view::npos) {
                return true;
            }
            begin = dot + 1;
        }
    }

    std::string ContribLocator::toString() const {
        std::string res = _package;
        if (!_version.isEmpty()) {
            res += '=';
            res += _version.toString();
        }
        if (!_id.empty()) {
            res += ':';
            if (!_category.empty()) {
                res += _category;
                res += '/';
            }
            res += _id;
        }
        return res;
    }

    ContribLocator ContribLocator::fromString(const std::string_view &token) {
        if (token.empty()) {
            return {};
        }

        ContribLocator result;

        // The reference splits on the single ":" that separates the package from the contribute.
        // Both separators are excluded from every identifier, so each can occur at most once and
        // its position is unambiguous.
        std::string_view left = token;
        const auto colon = token.find(':');
        if (colon != std::string_view::npos) {
            left = token.substr(0, colon);
            const auto right = token.substr(colon + 1);
            if (right.empty() || right.find(':') != std::string_view::npos) {
                return {};
            }

            // Right of the colon the slash count decides the shape, since neither a category nor
            // a contribute identifier may contain one.
            const auto slash = right.find('/');
            if (slash == std::string_view::npos) {
                result._id = right;
            } else {
                const auto id = right.substr(slash + 1);
                if (id.find('/') != std::string_view::npos) {
                    return {};
                }
                result._category = right.substr(0, slash);
                result._id = id;
                if (!isValidSegment(result._category)) {
                    return {};
                }
            }
            if (!isValidSegment(result._id)) {
                return {};
            }
        }

        if (left.empty()) {
            // A bare ":" names nothing, whereas ":singer/main" names a contribute of the package
            // being resolved against.
            return colon == std::string_view::npos ? ContribLocator() : result;
        }

        const auto equals = left.find('=');
        if (equals != std::string_view::npos) {
            const auto package = left.substr(0, equals);
            if (package.empty()) {
                // A version without a package to apply it to.
                return {};
            }
            const auto version = left.substr(equals + 1);
            if (!isValidVersion(version)) {
                return {};
            }
            // isValidVersion() is the stricter of the two, so this should not fail. Asking anyway
            // rather than dereferencing on the strength of that.
            const auto parsed = stdc::VersionNumber::fromString(version);
            if (!parsed) {
                return {};
            }
            result._package = package;
            result._version = *parsed;
        } else {
            result._package = left;
        }
        if (!isValidPackageId(result._package)) {
            return {};
        }
        return result;
    }

    bool ContribLocator::isValidSegment(const std::string_view &token) {
        if (token.empty()) {
            return false;
        }
        for (const auto ch : token) {
            // Spelled through stdc::str rather than <cctype>, whose answers follow LC_CTYPE and
            // are undefined for the negative char every non-ASCII UTF-8 byte is.
            const bool ok = stdc::str::is_alnum(ch) || ch == '_' || ch == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    bool ContribLocator::isValidPackageId(const std::string_view &token) {
        if (token.empty()) {
            return false;
        }
        size_t begin = 0;
        while (true) {
            const auto slash = token.find('/', begin);
            const auto segment = slash == std::string_view::npos
                                     ? token.substr(begin)
                                     : token.substr(begin, slash - begin);
            if (!isValidSegment(segment)) {
                return false;
            }
            if (slash == std::string_view::npos) {
                return true;
            }
            begin = slash + 1;
        }
    }

}
