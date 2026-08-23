#include "PackageDependency.h"

#include <set>
#include <string_view>

#include <stdcorelib/str.h>

#include <stdcorelib/str.h>

namespace srt {

    namespace {

        bool isIdentifierCharacter(char ch) {
            return stdc::str::is_alnum(ch) || ch == '_' || ch == '-';
        }

        bool isValidPackageIdentifier(std::string_view value) {
            if (value.empty()) {
                return false;
            }

            bool segmentHasCharacter = false;
            for (char ch : value) {
                if (ch == '/') {
                    if (!segmentHasCharacter) {
                        return false;
                    }
                    segmentHasCharacter = false;
                } else if (isIdentifierCharacter(ch)) {
                    segmentHasCharacter = true;
                } else {
                    return false;
                }
            }
            return segmentHasCharacter;
        }

        bool isValidVersion(std::string_view value) {
            if (value.empty()) {
                return false;
            }

            size_t componentCount = 0;
            size_t componentBegin = 0;
            while (componentBegin < value.size()) {
                const auto componentEnd = value.find('.', componentBegin);
                const auto component =
                    value.substr(componentBegin, componentEnd == std::string_view::npos
                                                     ? std::string_view::npos
                                                     : componentEnd - componentBegin);
                if (component.empty() || ++componentCount > 4 ||
                    (component.size() > 1 && component.front() == '0')) {
                    return false;
                }
                for (char ch : component) {
                    if (!stdc::str::is_digit(ch)) {
                        return false;
                    }
                }
                if (componentEnd == std::string_view::npos) {
                    return true;
                }
                componentBegin = componentEnd + 1;
            }
            return false;
        }

    }

    Expected<PackageDependency> PackageDependency::fromJsonValue(const JsonValue &value) {
        if (!value.isObject()) {
            return Error(Error::InvalidFormat, "dependency must be an object");
        }

        const auto &object = value.toObject();
        static const std::set<std::string_view> allowedKeys = {"id", "version"};
        for (const auto &item : object) {
            if (!allowedKeys.count(item.first)) {
                return Error(Error::InvalidFormat,
                             std::string("unknown dependency field \"") + item.first + '"');
            }
        }

        auto it = object.find("id");
        if (it == object.end() || !it->second.isString() ||
            !isValidPackageIdentifier(it->second.toString())) {
            return Error(Error::InvalidFormat, "dependency has a missing or invalid id field");
        }
        auto id = it->second.toString();

        it = object.find("version");
        if (it == object.end() || !it->second.isString()) {
            return Error(Error::InvalidFormat, "dependency has a missing or invalid version field");
        }

        const auto versionText = it->second.toString();
        if (!isValidVersion(versionText)) {
            return Error(Error::InvalidFormat, "dependency has an invalid version field");
        }
        const auto version = stdc::VersionNumber::fromString(versionText);
        if (!version || version->isEmpty()) {
            return Error(Error::InvalidFormat, "dependency version is not representable");
        }

        return PackageDependency(std::move(id), *version);
    }

}
