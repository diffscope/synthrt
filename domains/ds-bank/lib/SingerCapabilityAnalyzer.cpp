// SingerCapabilityAnalyzer.cpp - Parse-time mixable speakers / phonemes / languages
// analysis. Implements 03-dsbank-capability.md section 3.
//
// Called by VoicebankScanner after a SingerSnapshot is fully built (cross-package
// inference resolution requires the complete inference list). Format errors are
// recorded as warnings and skipped; loading is never blocked.

#include "SingerCapabilityAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/Support/JSON.h>

namespace ds::bank {

    namespace {

        // Case-insensitive substring check (used for vocoder className detection).
        bool icontains(const std::string &haystack, const std::string &needle) {
            if (needle.empty()) {
                return true;
            }
            if (haystack.size() < needle.size()) {
                return false;
            }
            const auto toLower = [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            };
            std::string hLower, nLower;
            hLower.reserve(haystack.size());
            nLower.reserve(needle.size());
            for (char c : haystack) hLower.push_back(toLower(c));
            for (char c : needle) nLower.push_back(toLower(c));
            return hLower.find(nLower) != std::string::npos;
        }

        bool isVocoderStage(const std::string &className) {
            // e.g. "ai.svs.VocoderInference" -> contains "vocoder"
            return icontains(className, "vocoder");
        }

        // Read a JSON object file and return its keys. On read/parse failure or
        // non-object root, returns false and appends a warning.
        bool readJsonObjectKeys(const std::filesystem::path &path,
                                std::vector<std::string> &outKeys,
                                std::vector<std::string> &warnings,
                                const std::string &stageLabel) {
            if (path.empty()) {
                return false;  // not configured; caller decides whether to warn
            }
            std::ifstream file(path);
            if (!file.is_open()) {
                warnings.emplace_back(stdc::formatN(
                    "%1: phoneme/language table missing or unreadable in stage '%2'",
                    stdc::path::to_utf8(path.filename()), stageLabel));
                return false;
            }
            std::stringstream ss;
            ss << file.rdbuf();
            std::string parseErr;
            auto root = srt::core::JsonValue::fromJson(ss.str(), true, &parseErr);
            if (!parseErr.empty() || !root.isObject()) {
                warnings.emplace_back(stdc::formatN(
                    "%1: invalid format in stage '%2'", stdc::path::to_utf8(path.filename()), stageLabel));
                return false;
            }
            for (const auto &[key, _] : root.toObject()) {
                outKeys.push_back(key);
            }
            std::sort(outKeys.begin(), outKeys.end());
            return true;
        }

        // Sorted-set intersection of two sorted vectors.
        std::vector<std::string> intersectSorted(const std::vector<std::string> &a,
                                                 const std::vector<std::string> &b) {
            std::vector<std::string> out;
            std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                                  std::back_inserter(out));
            return out;
        }

        // True if two sorted vectors are equal (same elements).
        bool sortedEqual(const std::vector<std::string> &a,
                         const std::vector<std::string> &b) {
            return a == b;
        }

        // Determine consistency from per-stage sets and the final intersection.
        // \p nonEmptyCount counts stages that contributed a set (excluding
        // format-error-skipped ones). \p hadSkip true if any stage was skipped.
        ConsistencyLevel judgeConsistency(const std::vector<std::vector<std::string>> &sets,
                                          const std::vector<std::string> &intersection,
                                          bool hadSkip) {
            if (intersection.empty()) {
                return ConsistencyLevel::Inconsistent;
            }
            if (hadSkip) {
                return ConsistencyLevel::Degraded;
            }
            for (const auto &s : sets) {
                if (!sortedEqual(s, intersection)) {
                    return ConsistencyLevel::Degraded;
                }
            }
            return ConsistencyLevel::Ideal;
        }

    } // namespace

    std::optional<SingerCapabilityReport> SingerCapabilityAnalyzer::analyze(
        const std::vector<SingerImportInfo> &imports,
        const std::vector<InferenceInfo> &inferences) {
        // Build a lookup of inferenceId -> InferenceInfo* (first match wins;
        // cross-package stage sharing is handled by the caller passing the
        // merged inference list).
        std::map<std::string, const InferenceInfo *> infById;
        for (const auto &inf : inferences) {
            if (!inf.id.empty()) {
                infById.emplace(inf.id, &inf);
            }
        }

        // Collect non-vocoder stages with their resolved InferenceInfo.
        struct ResolvedStage {
            const SingerImportInfo *import;
            const InferenceInfo *info;
        };
        std::vector<ResolvedStage> stages;
        std::vector<std::string> unresolvedWarnings;
        for (const auto &imp : imports) {
            const auto it = infById.find(imp.inferenceId);
            if (it == infById.end() || it->second == nullptr) {
                // HB-03: record unresolved import instead of silent skip.
                unresolvedWarnings.push_back(stdc::formatN(
                    "unresolved import: inferenceId=\"%1\"", imp.inferenceId));
                continue;  // unresolved import; nothing to analyze
            }
            if (isVocoderStage(it->second->className)) {
                continue;  // vocoder does not participate in speaker/phoneme mixing
            }
            stages.push_back({&imp, it->second});
        }

        if (stages.empty()) {
            // Pure G2P package or no non-vocoder inferences resolvable.
            return std::nullopt;
        }

        SingerCapabilityReport report;
        // HB-03: surface unresolved import warnings on the report so callers
        // can diagnose missing stages. Pure-G2P (stages empty) still returns
        // nullopt to preserve the existing contract (tst_capability_analyzer
        // "empty imports -> nullopt" / "only vocoder -> nullopt").
        report.speakerWarnings = std::move(unresolvedWarnings);

        // ---- Speaker analysis (§3.1) ----
        // Collect per-stage singer-domain speaker sets for constraining stages
        // (useSpeakerEmbedding=true). Non-constraining stages are recorded in
        // stages[] but do not reduce the intersection.
        std::vector<std::vector<std::string>> perStageSingerSpeakers;
        std::vector<std::string> speakerIntersection;
        bool speakerIntersectionInit = false;
        bool hasNonConstrainingStage = false;
        bool hasConstrainingStage = false;
        bool hiddenSizeMismatch = false;
        int referenceHiddenSize = 0;
        bool referenceHiddenSizeSet = false;

        for (const auto &st : stages) {
            StageCapability sc;
            sc.stageId = st.info->id;
            sc.className = st.info->className;
            sc.useSpeakerEmbedding = st.info->useSpeakerEmbedding;
            sc.hiddenSize = st.info->hiddenSize;
            sc.speakerMapping = st.import->speakerMapping;

            // Record model-domain speakers.
            for (const auto &[modelSpk, _] : st.info->speakerEmbeddings) {
                sc.speakers.push_back(modelSpk);
            }
            std::sort(sc.speakers.begin(), sc.speakers.end());

            // hiddenSize consistency check (ignore unspecified 0).
            if (st.info->hiddenSize > 0) {
                if (!referenceHiddenSizeSet) {
                    referenceHiddenSize = st.info->hiddenSize;
                    referenceHiddenSizeSet = true;
                } else if (st.info->hiddenSize != referenceHiddenSize) {
                    hiddenSizeMismatch = true;
                }
            }

            if (!st.info->useSpeakerEmbedding) {
                hasNonConstrainingStage = true;
                report.speakerWarnings.emplace_back(stdc::formatN(
                    "stage '%1' does not use speaker embedding; not constraining mixable speakers",
                    st.info->id));
            } else {
                hasConstrainingStage = true;
                // Compute singer-domain speakers that this stage supports.
                std::set<std::string> modelSet(sc.speakers.begin(), sc.speakers.end());
                std::vector<std::string> singerSpeakers;
                if (st.import->speakerMapping.empty()) {
                    // Identity mapping: singer-domain == model-domain.
                    singerSpeakers = sc.speakers;
                } else {
                    for (const auto &[singerSpk, modelSpk] : st.import->speakerMapping) {
                        if (modelSet.count(modelSpk)) {
                            singerSpeakers.push_back(singerSpk);
                        } else {
                            report.speakerWarnings.emplace_back(stdc::formatN(
                                "stage '%1': speaker '%2' maps to missing model speaker '%3'",
                                st.info->id, singerSpk, modelSpk));
                        }
                    }
                    std::sort(singerSpeakers.begin(), singerSpeakers.end());
                }
                perStageSingerSpeakers.push_back(singerSpeakers);
                if (!speakerIntersectionInit) {
                    speakerIntersection = singerSpeakers;
                    speakerIntersectionInit = true;
                } else {
                    speakerIntersection = intersectSorted(speakerIntersection, singerSpeakers);
                }
            }

            report.stages.push_back(std::move(sc));
        }

        report.mixableSpeakers = speakerIntersection;

        // Speaker consistency verdict.
        if (hiddenSizeMismatch) {
            report.speakerConsistency = ConsistencyLevel::Inconsistent;
            report.speakerWarnings.emplace_back(
                "hiddenSize mismatch across stages; speaker embedding dimensions are inconsistent");
            // Per 03-dsbank-capability.md §4: hiddenSize mismatch -> mixableSpeakers = ∅.
            // Embedding dimensions are incompatible; no singer can be safely mixed.
            report.mixableSpeakers.clear();
        } else if (!hasConstrainingStage) {
            // No stage constrains speakers; cannot verify, treat as Degraded.
            report.speakerConsistency = ConsistencyLevel::Degraded;
            report.speakerWarnings.emplace_back(
                "no stage uses speaker embedding; mixable speakers undetermined");
        } else if (speakerIntersection.empty()) {
            report.speakerConsistency = ConsistencyLevel::Inconsistent;
        } else if (hasNonConstrainingStage) {
            report.speakerConsistency = ConsistencyLevel::Degraded;
        } else {
            report.speakerConsistency =
                judgeConsistency(perStageSingerSpeakers, speakerIntersection, false);
        }

        // ---- Phoneme analysis (§3.2) ----
        std::vector<std::vector<std::string>> perStagePhonemes;
        std::vector<std::string> phonemeIntersection;
        bool phonemeIntersectionInit = false;
        bool phonemeHadSkip = false;
        for (size_t i = 0; i < stages.size(); ++i) {
            const auto &st = stages[i];
            std::vector<std::string> keys;
            const auto stageLabel = st.info->id;
            const auto hadPath = !st.info->phonemesPath.empty();
            const auto ok = readJsonObjectKeys(st.info->phonemesPath, keys,
                                               report.phonemeWarnings, stageLabel);
            if (!ok) {
                if (hadPath) {
                    phonemeHadSkip = true;  // path configured but unreadable
                } else {
                    // No phonemesPath: stage does not declare a phoneme table.
                    // Skip from intersection but do not count as a format error.
                    phonemeHadSkip = true;
                }
                continue;
            }
            perStagePhonemes.push_back(keys);
            if (!phonemeIntersectionInit) {
                phonemeIntersection = keys;
                phonemeIntersectionInit = true;
            } else {
                phonemeIntersection = intersectSorted(phonemeIntersection, keys);
            }
            // Fill stage detail.
            if (i < report.stages.size()) {
                report.stages[i].phonemes = std::move(keys);
            }
        }
        report.effectivePhonemes = phonemeIntersection;
        if (!phonemeIntersectionInit) {
            // No stage contributed a phoneme table.
            report.phonemeConsistency = ConsistencyLevel::Degraded;
            report.phonemeWarnings.emplace_back(
                "no stage declares a phoneme table; effective phonemes undetermined");
        } else {
            report.phonemeConsistency =
                judgeConsistency(perStagePhonemes, phonemeIntersection, phonemeHadSkip);
        }
        report.phonemeDegraded =
            report.phonemeConsistency != ConsistencyLevel::Ideal && !report.effectivePhonemes.empty();

        // ---- Language analysis (§3.2) ----
        std::vector<std::vector<std::string>> perStageLanguages;
        std::vector<std::string> languageIntersection;
        bool languageIntersectionInit = false;
        bool languageHadSkip = false;
        for (size_t i = 0; i < stages.size(); ++i) {
            const auto &st = stages[i];
            std::vector<std::string> keys;
            const auto stageLabel = st.info->id;
            const auto hadPath = !st.info->languagesPath.empty();
            const auto ok = readJsonObjectKeys(st.info->languagesPath, keys,
                                               report.languageWarnings, stageLabel);
            if (!ok) {
                if (hadPath) {
                    languageHadSkip = true;
                } else {
                    languageHadSkip = true;
                }
                continue;
            }
            perStageLanguages.push_back(keys);
            if (!languageIntersectionInit) {
                languageIntersection = keys;
                languageIntersectionInit = true;
            } else {
                languageIntersection = intersectSorted(languageIntersection, keys);
            }
            if (i < report.stages.size()) {
                report.stages[i].languages = std::move(keys);
            }
        }
        report.effectiveLanguages = languageIntersection;
        if (!languageIntersectionInit) {
            report.languageConsistency = ConsistencyLevel::Degraded;
            report.languageWarnings.emplace_back(
                "no stage declares a language table; effective languages undetermined");
        } else {
            report.languageConsistency =
                judgeConsistency(perStageLanguages, languageIntersection, languageHadSkip);
        }

        return report;
    }

} // namespace ds::bank
