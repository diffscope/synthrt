#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Expected.h>

#include <diffsinger/Bank/PackageStatus.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/SingerSnapshot.h>
#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// VoicebankScanner — Scan voicebank directories, parse desc.json,
    /// build SingerSnapshot list.
    ///
    /// Voicebank packages are installed flat (one directory per package,
    /// containing desc.json). G2P packages inside voicebank dirs use a
    /// different format and are handled by LanguageService, NOT by this
    /// scanner.
    ///
    /// Pure value: no global state, no background threads.
    class DSBANK_EXPORT VoicebankScanner {
    public:
        VoicebankScanner();
        ~VoicebankScanner();

        /// Set search paths. Multiple calls append; call clear() first to reset.
        void setSearchPaths(const std::vector<std::filesystem::path> &paths);

        /// Clear all search paths and cached results.
        void clear();

        /// Scan all search paths, parse desc.json for each package.
        /// Only finds voicebank packages — G2P packages are NOT scanned here.
        /// Returns status per package. Populates internal snapshots cache.
        srt::core::Expected<std::vector<PackageStatus>> refresh();

        /// Cached results after refresh().
        const std::vector<SingerSnapshot> &singers() const;

        /// Lookup singer by ref.
        srt::core::Expected<SingerSnapshot> singerSnapshot(
            const SingerRef &ref) const;

        /// Lookup SingerRef by singerId (scans all packages).
        srt::core::Expected<SingerRef> findSinger(
            const std::string &singerId) const;

        /// Lookup SingerRef by singerId with optional packageId/version
        /// filtering. Empty packageId/version means no filter (backward
        /// compatible with the single-arg overload).
        srt::core::Expected<SingerRef> findSinger(
            const std::string &singerId,
            const std::string &packageId,
            const std::string &version) const;

        /// Get the package directory for a packageId.
        /// Needed by callers to open packages in Runtime and to
        /// build the packageDirs map for LanguageService.
        std::filesystem::path packageDirectory(
            const std::string &packageId) const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace ds::bank
