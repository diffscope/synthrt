#ifndef SYNTHRT_PACKAGEDEPENDENCY_H
#define SYNTHRT_PACKAGEDEPENDENCY_H

#include <string>
#include <utility>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    /// Describes one required direct Package dependency.
    struct SYNTHRT_EXPORT PackageDependency {
        inline PackageDependency() = default;

        inline PackageDependency(std::string id, stdc::VersionNumber version)
            : id(std::move(id)), version(std::move(version)) {
        }

        inline bool operator==(const PackageDependency &other) const {
            return id == other.id && version == other.version;
        }

        inline bool operator!=(const PackageDependency &other) const {
            return !(*this == other);
        }

        static Expected<PackageDependency> fromJsonValue(const JsonValue &value);

    public:
        std::string id;
        stdc::VersionNumber version;
    };

}

#endif // SYNTHRT_PACKAGEDEPENDENCY_H
