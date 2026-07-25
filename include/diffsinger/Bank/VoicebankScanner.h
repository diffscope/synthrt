#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Expected.h>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/PackageStatus.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/SingerSnapshot.h>
#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// One (version, path) entry returned by VoicebankScanner::packageDirectories().
    /// Replaces the legacy single-path packageDirectory(packageId) API which
    /// lost version info: multi-version same-packageId voicebanks can now
    /// coexist (V3-01 §1.6, 5th layer discovered in reverse-verification).
    struct DSBANK_EXPORT PackageDirectoryResult {
        stdc::VersionNumber version;
        std::filesystem::path path;
    };

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

        /// TD-01 (D-39 #2): Cached manifests for valid packages after
        /// refresh(). Ordered by discovery, matching the order of valid
        /// entries in the PackageStatus vector returned by refresh(). Invalid
        /// packages (parse failures) contribute only their PackageStatus.error
        /// and have no manifest here. Lets callers read the full manifest
        /// (name/description/author/license/singers/speakers/languages/
        /// inferences) without re-parsing desc.json.
        const std::vector<PackageManifest> &manifests() const;

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

        /// Get all (version, path) entries for a packageId. Multi-version
        /// same-packageId voicebanks all survive in the returned vector, in the
        /// order they were discovered during refresh(). Empty vector when the
        /// packageId is unknown. This is the version-aware replacement for
        /// packageDirectory(packageId) (V3-01 §1.6).
        std::vector<PackageDirectoryResult> packageDirectories(
            const std::string &packageId) const;

        /// Legacy: get a single package directory for a packageId. Returns the
        /// first matching entry discovered (or empty path when unknown).
        /// Multi-version same-packageId voicebanks collapse to one entry here —
        /// callers needing full version isolation must migrate to
        /// packageDirectories(packageId).
        /// Note: returns empty path when the packageId has multiple versions
        /// installed (D-42 multi-version ambiguity guard). Callers must
        /// migrate to packageDirectories() to disambiguate explicitly.
        [[deprecated("Use packageDirectories(packageId). Will be removed in Level=3.")]]
        std::filesystem::path packageDirectory(
            const std::string &packageId) const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace ds::bank
