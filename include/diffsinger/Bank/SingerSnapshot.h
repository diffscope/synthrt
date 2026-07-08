#pragma once

#include <string>
#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/ResolutionState.h>
#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// SingerSnapshot - Immutable singer snapshot that the host can cache.
    ///
    /// Contains runtime resolution state in addition to singer metadata.
    ///
    /// \see 02-module-contracts.md section 5.2
    struct DSBANK_EXPORT SingerSnapshot {
        SingerRef ref;
        std::string name;
        ResolutionState resolutionState = ResolutionState::Pending;
        srt::core::Diagnostic resolutionError;  ///< Reason for Missing/Pending
        double phonemeLength = 48.0;
        std::vector<std::string> languages;
        std::vector<std::string> speakerIds;
        std::string defaultLanguage;
        std::vector<std::string> inferenceIds;  ///< Available inference capabilities
        std::string version;  ///< v2: version string (normalized, mirrors ref.version)
    };

} // namespace ds::bank
