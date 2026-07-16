#pragma once

#include <optional>
#include <vector>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/SingerCapabilityReport.h>
#include <diffsinger/Bank/SingerManifest.h>

namespace ds::bank {

    /// SingerCapabilityAnalyzer - Computes a SingerCapabilityReport at parse
    /// time from a singer's imports and the package's available inferences.
    ///
    /// Called by VoicebankScanner after the SingerSnapshot is fully built
    /// (cross-package inference resolution requires the full inference list).
    ///
    /// \see 03-dsbank-capability.md section 3
    class DSBANK_EXPORT SingerCapabilityAnalyzer {
    public:
        /// Analyze a singer's capability. Returns nullopt when no non-vocoder
        /// inference is referenced (pure G2P package). Format errors in
        /// phonemes/languages files are recorded as warnings and skipped
        /// (conservative: never blocks loading).
        static std::optional<SingerCapabilityReport> analyze(
            const std::vector<SingerImportInfo> &imports,
            const std::vector<InferenceInfo> &inferences);
    };

} // namespace ds::bank
