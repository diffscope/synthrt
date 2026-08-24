#pragma once

#include <optional>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/DisplayText.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/ResolutionState.h>
#include <diffsinger/Bank/LanguageInfo.h>
#include <diffsinger/Bank/SpeakerInfo.h>
#include <diffsinger/Bank/SingerCapabilityReport.h>
#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// SingerSnapshot - Immutable singer snapshot that the host can cache.
    ///
    /// Contains runtime resolution state in addition to singer metadata.
    ///
    /// \see 02-module-contracts.md section 5.2
    struct DSBANK_EXPORT SingerSnapshot {
        SingerRef ref;
        /// 多语言显示名（ds-spec 2.4）：全部翻译随快照保留，宿主持久化后按
        /// 自有匹配策略以 name.locales()/name.text(key) 取词（缺键返回
        /// nullptr，回退 text() 的时机归宿主），切换语言无需重新扫描。
        srt::core::DisplayText name;
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

        /// Parse-time mixable speakers / phonemes / languages analysis.
        /// Empty for singers without inference (pure G2P packages).
        std::optional<SingerCapabilityReport> capabilityReport;
    };

} // namespace ds::bank
