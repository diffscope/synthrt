#pragma once

#include <string>
#include <string_view>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// SingerRef - Stable reference to a singer declared by a DiffSinger package.
    ///
    /// \see 02-module-contracts.md section 5.1
    struct DSBANK_EXPORT SingerRef {
        std::string packageId;
        std::string singerId;
        std::string version;  ///< v2: version string (normalized by VersionNumber::toString())

        SingerRef() = default;
        SingerRef(std::string packageId, std::string singerId)
            : packageId(std::move(packageId)), singerId(std::move(singerId)) {}
        SingerRef(std::string packageId, std::string singerId, std::string version)
            : packageId(std::move(packageId)), singerId(std::move(singerId)),
              version(std::move(version)) {}

        /// Returns the canonical string form: \c "packageId:singerId".
        std::string toString() const;

        /// Parses a canonical \c "packageId:singerId" string into a SingerRef.
        static SingerRef parse(std::string_view s);
    };

} // namespace ds::bank
