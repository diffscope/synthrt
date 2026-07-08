#pragma once

#include <filesystem>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/ValidationReport.h>
#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// PackageValidator - Validates a DiffSinger package (either an in-memory
    /// \c PackageManifest or a directory on disk) against one of the supported
    /// schema versions (V1 through V10).
    class DSBANK_EXPORT PackageValidator {
    public:
        enum class SchemaVersion {
            V1,
            V2,
            V3,
            V4,
            V5,
            V6,
            V7,
            V8,
            V9,
            V10,
        };

    public:
        /// Validates an in-memory \c PackageManifest descriptor against \p version.
        ValidationReport validate(const PackageManifest &info, SchemaVersion version) const;

        /// Validates the package located at \p packageDir by parsing its manifest
        /// first, then validating the resulting \c PackageManifest against \p version.
        ValidationReport validatePackage(const std::filesystem::path &packageDir,
                                         SchemaVersion version) const;
    };

}
