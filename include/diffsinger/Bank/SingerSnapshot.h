#pragma once

#include <string>
#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/ResolutionState.h>
#include <diffsinger/Bank/LanguageInfo.h>
#include <diffsinger/Bank/SpeakerInfo.h>
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
        std::vector<std::string> languages;      ///< Language IDs (backward compat)
        std::vector<std::string> speakerIds;      ///< Speaker IDs (backward compat)

        // Complete objects (R1): preserve full LanguageInfo/SpeakerInfo so lite
        // hosts can avoid re-invoking PackageParser.
        std::vector<LanguageInfo> languageInfos;  ///< Full language info objects
        std::vector<SpeakerInfo> speakerInfos;    ///< Full speaker info objects

        std::string defaultLanguage;
        std::vector<std::string> inferenceIds;  ///< Available inference capabilities
        std::string version;  ///< v2: version string (normalized, mirrors ref.version)
    };

} // namespace ds::bank
