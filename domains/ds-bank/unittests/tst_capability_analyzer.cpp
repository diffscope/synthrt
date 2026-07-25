// Regression tests for ds::bank::SingerCapabilityAnalyzer (v3 §2.1 mixableSpeakers
// + §2.2 effectivePhonemes).
//
// Covers 07-test-matrix.md:
//   §2.1: mixableSpeakers analysis (Ideal / Degraded / Inconsistent)
//   §2.2: effectivePhonemes intersection
//   pure G2P package returns nullopt
//
// All test data is constructed in-memory; phoneme tables use temp JSON files.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/SingerCapabilityReport.h>
#include <diffsinger/Bank/SingerManifest.h>

#include "SingerCapabilityAnalyzer.h"

using namespace ds::bank;

namespace {

    // RAII temp directory: created on construction, removed on destruction.
    struct TempDir {
        std::filesystem::path path;
        TempDir() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() /
                  ("ds-bank-cap-" + std::to_string(stamp));
            std::filesystem::create_directories(path);
        }
        ~TempDir() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
        TempDir(const TempDir &) = delete;
        TempDir &operator=(const TempDir &) = delete;
    };

    // Write a JSON object file whose keys form the phoneme/language table.
    std::filesystem::path writeJsonObject(const TempDir &dir,
                                          const std::string &fileName,
                                          const std::vector<std::string> &keys) {
        auto filePath = dir.path / fileName;
        std::ofstream file(filePath);
        file << "{";
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i) file << ",";
            file << "\"" << keys[i] << "\":1";
        }
        file << "}";
        return filePath;
    }

    // Build an InferenceInfo for a non-vocoder stage.
    InferenceInfo makeInference(const std::string &id,
                                const std::vector<std::string> &speakers,
                                int hiddenSize,
                                bool useSpeakerEmbedding,
                                const std::filesystem::path &phonemesPath = {}) {
        InferenceInfo inf;
        inf.id = id;
        inf.className = "ai.svs.DiffSingerInference";
        inf.hiddenSize = hiddenSize;
        inf.useSpeakerEmbedding = useSpeakerEmbedding;
        for (const auto &spk : speakers) {
            inf.speakerEmbeddings.emplace(spk, std::filesystem::path{});
        }
        inf.phonemesPath = phonemesPath;
        return inf;
    }

    // Build a SingerImportInfo referencing an inference by id, with identity
    // speaker mapping (empty map => singer-domain == model-domain).
    SingerImportInfo makeImport(const std::string &inferenceId,
                                std::map<std::string, std::string> mapping = {}) {
        SingerImportInfo imp;
        imp.inferenceId = inferenceId;
        imp.speakerMapping = std::move(mapping);
        return imp;
    }

} // namespace

// ===========================================================================
// §2.1 mixableSpeakers analysis
// ===========================================================================

TEST_CASE("mixableSpeakers: single spk all stages resolvable -> Ideal",
          "[capability][mixable-speakers]") {
    std::vector<InferenceInfo> inferences = {
        makeInference("dur", {"S1"}, 256, true),
        makeInference("ac", {"S1"}, 256, true),
    };
    std::vector<SingerImportInfo> imports = {
        makeImport("dur"),
        makeImport("ac"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(report.has_value());
    REQUIRE(report->mixableSpeakers == std::vector<std::string>{"S1"});
    REQUIRE(report->speakerConsistency == ConsistencyLevel::Ideal);
}

TEST_CASE("mixableSpeakers: duration missing S2 -> Degraded",
          "[capability][mixable-speakers]") {
    std::vector<InferenceInfo> inferences = {
        makeInference("dur", {"S1", "S2"}, 256, true),
        makeInference("ac", {"S1"}, 256, true),
    };
    std::vector<SingerImportInfo> imports = {
        makeImport("dur"),
        makeImport("ac"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(report.has_value());
    // Intersection of {S1,S2} and {S1} is {S1}; not all identical -> Degraded.
    REQUIRE(report->mixableSpeakers == std::vector<std::string>{"S1"});
    REQUIRE(report->speakerConsistency == ConsistencyLevel::Degraded);
}

TEST_CASE("mixableSpeakers: hiddenSize mismatch -> Inconsistent",
          "[capability][mixable-speakers]") {
    std::vector<InferenceInfo> inferences = {
        makeInference("dur", {"S1"}, 256, true),
        makeInference("ac", {"S1"}, 512, true),
    };
    std::vector<SingerImportInfo> imports = {
        makeImport("dur"),
        makeImport("ac"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(report.has_value());
    REQUIRE(report->speakerConsistency == ConsistencyLevel::Inconsistent);
    // Per 03-dsbank-capability.md §4: hiddenSize mismatch -> mixableSpeakers = ∅.
    REQUIRE(report->mixableSpeakers.empty());
    // A hiddenSize mismatch warning must be recorded.
    bool hasHiddenSizeWarning = false;
    for (const auto &w : report->speakerWarnings) {
        if (w.find("hiddenSize") != std::string::npos) {
            hasHiddenSizeWarning = true;
            break;
        }
    }
    REQUIRE(hasHiddenSizeWarning);
}

TEST_CASE("mixableSpeakers: all stages identical speakers -> Ideal full set",
          "[capability][mixable-speakers]") {
    std::vector<InferenceInfo> inferences = {
        makeInference("dur", {"S1", "S2"}, 256, true),
        makeInference("ac", {"S1", "S2"}, 256, true),
    };
    std::vector<SingerImportInfo> imports = {
        makeImport("dur"),
        makeImport("ac"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(report.has_value());
    REQUIRE(report->mixableSpeakers == std::vector<std::string>{"S1", "S2"});
    REQUIRE(report->speakerConsistency == ConsistencyLevel::Ideal);
}

TEST_CASE("mixableSpeakers: vocoder stage excluded from intersection",
          "[capability][mixable-speakers]") {
    // Vocoder declares extra speakers but must not participate.
    std::vector<InferenceInfo> inferences = {
        makeInference("dur", {"S1"}, 256, true),
        makeInference("ac", {"S1"}, 256, true),
        makeInference("voc", {"S1", "S3"}, 0, true),
    };
    // Mark the third inference as a vocoder via className.
    inferences[2].className = "ai.svs.VocoderInference";
    std::vector<SingerImportInfo> imports = {
        makeImport("dur"),
        makeImport("ac"),
        makeImport("voc"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(report.has_value());
    // Only dur + ac participate; S3 from vocoder is excluded.
    REQUIRE(report->mixableSpeakers == std::vector<std::string>{"S1"});
    REQUIRE(report->speakerConsistency == ConsistencyLevel::Ideal);
    // Only 2 non-vocoder stages recorded.
    REQUIRE(report->stages.size() == 2);
}

TEST_CASE("mixableSpeakers: pure G2P package (no inference) -> nullopt",
          "[capability][mixable-speakers]") {
    std::vector<InferenceInfo> inferences;
    std::vector<SingerImportInfo> imports;

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(!report.has_value());
}

TEST_CASE("mixableSpeakers: only vocoder inferences -> nullopt",
          "[capability][mixable-speakers]") {
    std::vector<InferenceInfo> inferences = {
        makeInference("voc", {"S1"}, 0, true),
    };
    inferences[0].className = "ai.svs.VocoderInference";
    std::vector<SingerImportInfo> imports = {
        makeImport("voc"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(!report.has_value());
}

// ===========================================================================
// §2.2 effectivePhonemes intersection
// ===========================================================================

TEST_CASE("effectivePhonemes: all stages same phonemes -> Ideal",
          "[capability][effective-phonemes]") {
    TempDir dir;
    auto phDur = writeJsonObject(dir, "dur_phonemes.json", {"a", "e", "i"});
    auto phAc = writeJsonObject(dir, "ac_phonemes.json", {"a", "e", "i"});

    std::vector<InferenceInfo> inferences = {
        makeInference("dur", {"S1"}, 256, true, phDur),
        makeInference("ac", {"S1"}, 256, true, phAc),
    };
    std::vector<SingerImportInfo> imports = {
        makeImport("dur"),
        makeImport("ac"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(report.has_value());
    REQUIRE(report->effectivePhonemes == std::vector<std::string>{"a", "e", "i"});
    REQUIRE(report->phonemeConsistency == ConsistencyLevel::Ideal);
}

TEST_CASE("effectivePhonemes: duration missing 'e' -> Degraded, 'e' excluded",
          "[capability][effective-phonemes]") {
    TempDir dir;
    auto phDur = writeJsonObject(dir, "dur_phonemes.json", {"a", "i"});
    auto phAc = writeJsonObject(dir, "ac_phonemes.json", {"a", "e", "i"});

    std::vector<InferenceInfo> inferences = {
        makeInference("dur", {"S1"}, 256, true, phDur),
        makeInference("ac", {"S1"}, 256, true, phAc),
    };
    std::vector<SingerImportInfo> imports = {
        makeImport("dur"),
        makeImport("ac"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(report.has_value());
    REQUIRE(report->effectivePhonemes == std::vector<std::string>{"a", "i"});
    REQUIRE(report->phonemeConsistency == ConsistencyLevel::Degraded);
}

TEST_CASE("effectivePhonemes: phonemesPath missing -> Degraded skip",
          "[capability][effective-phonemes]") {
    TempDir dir;
    auto phAc = writeJsonObject(dir, "ac_phonemes.json", {"a", "e", "i"});

    std::vector<InferenceInfo> inferences = {
        makeInference("dur", {"S1"}, 256, true, {}),  // no phonemesPath
        makeInference("ac", {"S1"}, 256, true, phAc),
    };
    std::vector<SingerImportInfo> imports = {
        makeImport("dur"),
        makeImport("ac"),
    };

    auto report = SingerCapabilityAnalyzer::analyze(imports, inferences);
    REQUIRE(report.has_value());
    // Only ac contributes; intersection = {a,e,i} but one stage skipped -> Degraded.
    REQUIRE(report->effectivePhonemes == std::vector<std::string>{"a", "e", "i"});
    REQUIRE(report->phonemeConsistency == ConsistencyLevel::Degraded);
}
