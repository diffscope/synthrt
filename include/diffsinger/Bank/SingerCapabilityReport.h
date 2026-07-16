#pragma once

#include <map>
#include <string>
#include <vector>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// StageCapability - Per-stage capability snapshot used by the analyzer.
    struct DSBANK_EXPORT StageCapability {
        std::string stageId;
        std::string className;
        bool useSpeakerEmbedding = false;
        int hiddenSize = 0;
        std::vector<std::string> speakers;  ///< model-domain speaker names
        std::vector<std::string> phonemes;
        std::vector<std::string> languages;
        std::map<std::string, std::string> speakerMapping;  ///< singer-domain -> model-domain
    };

    /// ConsistencyLevel - Cross-stage consistency verdict.
    enum class ConsistencyLevel {
        Ideal,        ///< all stages identical
        Degraded,     ///< intersection non-empty but not all identical
        Inconsistent, ///< intersection empty or hiddenSize mismatch
    };

    /// SingerCapabilityReport - Parse-time mixable speakers / phonemes / languages
    /// analysis for a singer. Produced by SingerCapabilityAnalyzer after the
    /// SingerSnapshot is fully built.
    ///
    /// \see 03-dsbank-capability.md section 2
    struct DSBANK_EXPORT SingerCapabilityReport {
        std::vector<std::string> mixableSpeakers;  ///< singer-domain, intersection of non-vocoder stages
        ConsistencyLevel speakerConsistency = ConsistencyLevel::Ideal;
        std::vector<std::string> speakerWarnings;

        std::vector<std::string> effectivePhonemes;  ///< non-vocoder stage intersection
        ConsistencyLevel phonemeConsistency = ConsistencyLevel::Ideal;
        std::vector<std::string> phonemeWarnings;
        bool phonemeDegraded = false;

        std::vector<std::string> effectiveLanguages;
        ConsistencyLevel languageConsistency = ConsistencyLevel::Ideal;
        std::vector<std::string> languageWarnings;

        std::vector<StageCapability> stages;  ///< per-stage detail
    };

} // namespace ds::bank
