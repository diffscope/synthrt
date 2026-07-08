#pragma once

#include <filesystem>

#include <synthrt/Core/Support/Expected.h>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// PackageParser - Reads a DiffSinger package directory and produces a
    /// \c PackageManifest descriptor from its standard \c desc.json manifest.
    ///
    /// \c Strict mode enforces required fields and rejects unknown keys, while
    /// \c Relaxed mode tolerates missing optional metadata and forward-compatible
    /// fields (suitable for scanning untrusted or partially-authored packages).
    class DSBANK_EXPORT PackageParser {
    public:
        enum class ParseMode {
            Strict,
            Relaxed,
        };

    public:
        /// Parses the package manifest (\c desc.json) located in \p packageDir.
        /// Returns the parsed \c PackageManifest on success, or an \c Error on failure.
        srt::core::Expected<PackageManifest>
            parsePackage(const std::filesystem::path &packageDir,
                         ParseMode mode = ParseMode::Strict) const;
    };

}
