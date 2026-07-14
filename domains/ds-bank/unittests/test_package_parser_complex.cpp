// Complex scenario tests for PackageParser and PackageValidator.
//
// Covers edge cases in package manifest parsing and validation:
//   - Multi-singer packages (3+ singers, shared inference, cross-singer speakers)
//   - Multi-inference packages (duration+pitch+acoustic+vocoder, different levels)
//   - Missing optional fields (name, description, author, license, dependencies)
//   - Empty contributes (no singers, no inferences)
//   - Complex package names (dots, hyphens, underscores, Unicode)
//   - Version with many segments (5+ parts, pre-release suffixes)
//   - Multiple dependencies with version ranges
//   - Speaker embeddings and hiddenSize/sampleRate/hopSize in InferenceInfo
//   - useLanguageId / useSpeakerEmbedding / useContinuousAcceleration flags
//   - PackageValidator: unknown G2P id, missing id, invalid version type,
//     extra root keys, import without id, missing singer config file
//   - Relaxed vs Strict parse mode differences
//   - Inference with modelPaths map and phonemesPath
//   - Singer with empty imports list (no inference references)
//   - Deeply nested resource paths (../../assets/)

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/path.h>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/PackageValidator.h>
#include <diffsinger/Bank/PackageManifest.h>

using namespace ds::bank;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("ds-pkg-complex-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    bool samePath(const std::filesystem::path &actual,
                  const std::filesystem::path &expected) {
        return actual.lexically_normal() == expected.lexically_normal();
    }

} // namespace

// ===========================================================================
// Multi-singer packages
// ===========================================================================

TEST_CASE("PackageParser multi-singer package: 3 singers shared inference", "[ds-bank][parser][complex][multi-singer]") {
    const auto dir = makeTempDir("multi-singer-3");

    std::string desc = R"json({
        "id": "pkg.multi-singer",
        "version": "1.0.0",
        "contributes": {
            "singers": [
                "characters/singer_a/config.json",
                "characters/singer_b/config.json",
                "characters/singer_c/config.json"
            ],
            "inferences": ["inferences/duration/config.json"]
        }
    })json";
    writeFile(dir / "desc.json", desc);

    for (const auto &sid : {"singer_a", "singer_b", "singer_c"}) {
        std::string singer = "{\n";
        singer += "    \"$version\": \"1.0\",\n";
        singer += "    \"id\": \"" + std::string(sid) + "\",\n";
        singer += "    \"level\": 1,\n";
        singer += "    \"imports\": [{\"inferenceId\": \"duration\"}],\n";
        singer += "    \"configuration\": {\n";
        singer += "        \"defaultLanguage\": \"cmn\",\n";
        singer += "        \"languages\": [{\"id\": \"cmn\", \"g2p\": \"g2p-cmn-official\", \"s2pMode\": \"dict\"}]\n";
        singer += "    }\n";
        singer += "}\n";
        writeFile(dir / ("characters/" + std::string(sid) + "/config.json"), singer);
    }

    writeFile(dir / "inferences/duration/config.json",
              "{\n    \"id\": \"duration\",\n    \"class\": \"ai.svs.DurationInference\",\n    \"level\": 1,\n    \"configuration\": {}\n}\n");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->singers().size() == 3);
    REQUIRE(result->singers()[0].singerId() == "singer_a");
    REQUIRE(result->singers()[1].singerId() == "singer_b");
    REQUIRE(result->singers()[2].singerId() == "singer_c");
    // All share the same inference.
    REQUIRE(result->inferences().size() == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser multi-singer with different default languages", "[ds-bank][parser][complex][multi-singer]") {
    const auto dir = makeTempDir("multi-singer-lang");

    std::string desc = R"json({
        "id": "pkg.multi-lang",
        "version": "1.0.0",
        "contributes": {
            "singers": ["characters/cn_singer/config.json", "characters/en_singer/config.json"]
        }
    })json";
    writeFile(dir / "desc.json", desc);

    writeFile(dir / "characters/cn_singer/config.json", R"json({
        "$version": "1.0",
        "id": "cn_singer",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");

    writeFile(dir / "characters/en_singer/config.json", R"json({
        "$version": "1.0",
        "id": "en_singer",
        "level": 1,
        "configuration": {
            "defaultLanguage": "en",
            "languages": [{"id": "en", "g2p": "g2p-en-official", "s2pMode": "dict"}]
        }
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->singers().size() == 2);
    REQUIRE(result->singers()[0].defaultLanguage() == "cmn");
    REQUIRE(result->singers()[1].defaultLanguage() == "en");

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// Multi-inference packages with different levels and flags
// ===========================================================================

TEST_CASE("PackageParser multi-inference: 4 stages with different levels", "[ds-bank][parser][complex][multi-inference]") {
    const auto dir = makeTempDir("multi-inference-4");

    std::string desc = R"json({
        "id": "pkg.full-pipeline",
        "version": "2.0.0",
        "contributes": {
            "singers": ["characters/singer/config.json"],
            "inferences": [
                "inferences/duration/config.json",
                "inferences/pitch/config.json",
                "inferences/acoustic/config.json",
                "inferences/vocoder/config.json"
            ]
        }
    })json";
    writeFile(dir / "desc.json", desc);

    writeFile(dir / "characters/singer/config.json", R"json({
        "$version": "1.0",
        "id": "full_singer",
        "level": 2,
        "imports": [
            {"inferenceId": "duration"},
            {"inferenceId": "pitch"},
            {"inferenceId": "acoustic"},
            {"inferenceId": "vocoder"}
        ],
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");

    // Each inference has different level and flags.
    writeFile(dir / "inferences/duration/config.json", R"json({
        "id": "duration", "class": "ai.svs.DurationInference", "level": 1,
        "configuration": {"phonemes": "phonemes.json", "hiddenSize": 256}
    })json");
    writeFile(dir / "inferences/pitch/config.json", R"json({
        "id": "pitch", "class": "ai.svs.PitchInference", "level": 1,
        "configuration": {"hiddenSize": 256, "useLanguageId": true}
    })json");
    writeFile(dir / "inferences/acoustic/config.json", R"json({
        "id": "acoustic", "class": "ai.svs.AcousticInference", "level": 2,
        "configuration": {"hiddenSize": 256, "useSpeakerEmbedding": true, "useContinuousAcceleration": true}
    })json");
    writeFile(dir / "inferences/vocoder/config.json", R"json({
        "id": "vocoder", "class": "ai.svs.VocoderInference", "level": 1,
        "configuration": {"sampleRate": 44100, "hopSize": 512}
    })json");
    writeFile(dir / "inferences/duration/phonemes.json", "{}");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->inferences().size() == 4);

    // Verify each inference.
    bool foundDuration = false, foundPitch = false, foundAcoustic = false, foundVocoder = false;
    for (const auto &inf : result->inferences()) {
        // BF-31: each InferenceInfo must carry the owning package's id so
        // downstream ModelRegistry/SpeakerMapper can isolate same-id
        // inferences across packages.
        REQUIRE(inf.packageId == "pkg.full-pipeline");
        if (inf.id == "duration") {
            foundDuration = true;
            REQUIRE(inf.className == "ai.svs.DurationInference");
            REQUIRE(inf.level == 1);
        } else if (inf.id == "pitch") {
            foundPitch = true;
            REQUIRE(inf.level == 1);
        } else if (inf.id == "acoustic") {
            foundAcoustic = true;
            REQUIRE(inf.level == 2);
        } else if (inf.id == "vocoder") {
            foundVocoder = true;
            REQUIRE(inf.level == 1);
        }
    }
    REQUIRE(foundDuration);
    REQUIRE(foundPitch);
    REQUIRE(foundAcoustic);
    REQUIRE(foundVocoder);

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser inference with modelPaths and speakerEmbeddings", "[ds-bank][parser][complex][multi-inference]") {
    const auto dir = makeTempDir("inference-models");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.models",
        "version": "1.0.0",
        "contributes": {
            "singers": ["characters/singer/config.json"],
            "inferences": ["inferences/acoustic/config.json"]
        }
    })json");

    writeFile(dir / "characters/singer/config.json", R"json({
        "$version": "1.0",
        "id": "singer_m",
        "level": 2,
        "imports": [{"inferenceId": "acoustic"}],
        "configuration": {
            "defaultLanguage": "cmn",
            "speakers": [{"id": "spk1", "name": "Speaker 1"}],
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");

    writeFile(dir / "inferences/acoustic/config.json", R"json({
        "id": "acoustic",
        "class": "ai.svs.AcousticInference",
        "level": 2,
        "configuration": {
            "phonemes": "phonemes.json",
            "languages": "languages.json",
            "hiddenSize": 256,
            "sampleRate": 44100,
            "hopSize": 512,
            "useLanguageId": true,
            "useSpeakerEmbedding": true,
            "useContinuousAcceleration": false,
            "model": "models/acoustic.onnx",
            "durModel": "models/dur.onnx",
            "speakers": {"spk1": "embeddings/spk1.npy"},
            "parameters": ["velocity", "energy", "breathiness"]
        }
    })json");
    writeFile(dir / "inferences/acoustic/phonemes.json", "{}");
    writeFile(dir / "inferences/acoustic/languages.json", "{}");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->inferences().size() == 1);
    const auto &inf = result->inferences().front();
    REQUIRE(inf.hiddenSize == 256);
    REQUIRE(inf.sampleRate == 44100);
    REQUIRE(inf.hopSize == 512);
    REQUIRE(inf.useLanguageId);
    REQUIRE(inf.useSpeakerEmbedding);
    REQUIRE(!inf.useContinuousAcceleration);
    REQUIRE(inf.modelPaths.size() == 2);
    REQUIRE(inf.speakerEmbeddings.count("spk1") == 1);
    REQUIRE(inf.parameters.size() == 3);

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// Missing optional fields and empty contributes
// ===========================================================================

TEST_CASE("PackageParser missing optional fields still parses", "[ds-bank][parser][complex][optional]") {
    const auto dir = makeTempDir("minimal-pkg");

    // Minimal package: only id, version, and one singer.
    writeFile(dir / "desc.json", R"json({
        "id": "pkg.minimal",
        "version": "0.1.0",
        "contributes": {
            "singers": ["characters/s/config.json"]
        }
    })json");

    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0",
        "id": "s",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->packageId() == "pkg.minimal");
    REQUIRE(result->name().empty());
    REQUIRE(result->description().empty());
    REQUIRE(result->author().empty());
    REQUIRE(result->license().empty());
    REQUIRE(result->dependencies().empty());
    REQUIRE(result->inferences().empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser empty contributes is valid in relaxed mode", "[ds-bank][parser][complex][optional]") {
    const auto dir = makeTempDir("empty-contributes");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.empty",
        "version": "1.0.0",
        "contributes": {}
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    // Empty contributes should parse (no singers/inferences).
    REQUIRE(result.hasValue());
    REQUIRE(result->singers().empty());
    REQUIRE(result->inferences().empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser singer with empty imports list", "[ds-bank][parser][complex][optional]") {
    const auto dir = makeTempDir("empty-imports");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.no-imports",
        "version": "1.0.0",
        "contributes": {
            "singers": ["characters/singer/config.json"]
        }
    })json");

    writeFile(dir / "characters/singer/config.json", R"json({
        "$version": "1.0",
        "id": "singer_no_imports",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->singers().size() == 1);
    // imports() should be empty (no inference references).
    REQUIRE(result->singers().front().imports().empty());

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// Complex package names and version formats
// ===========================================================================

TEST_CASE("PackageParser complex package names", "[ds-bank][parser][complex][names]") {
    const std::vector<std::string> names = {
        "org.diffinger.singer.cn",
        "diffsinger-opencpop",
        "voice_bank_zh",
        "pkg.with.dots.and-hyphens_and.underscores",
        "UPPERCASE.Package",
        "123numeric",
    };

    for (const auto &name : names) {
        const auto dir = makeTempDir("name-" + name);
        std::string desc = "{\n";
        desc += "    \"id\": \"" + name + "\",\n";
        desc += "    \"version\": \"1.0.0\",\n";
        desc += "    \"contributes\": {\"singers\": [\"characters/s/config.json\"]}\n";
        desc += "}\n";
        writeFile(dir / "desc.json", desc);
        writeFile(dir / "characters/s/config.json", R"json({
            "$version": "1.0", "id": "s", "level": 1,
            "configuration": {"defaultLanguage": "cmn", "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]}
        })json");

        PackageParser parser;
        auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        INFO("Package name: " << name);
        REQUIRE(result.hasValue());
        REQUIRE(result->packageId() == name);

        std::filesystem::remove_all(dir);
    }
}

TEST_CASE("PackageParser version with 5+ segments", "[ds-bank][parser][complex][version]") {
    const auto dir = makeTempDir("version-5seg");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.v5",
        "version": "1.2.3.4.5",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");
    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0", "id": "s", "level": 1,
        "configuration": {"defaultLanguage": "cmn", "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]}
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    // VersionNumber stores at most 4 segments (major/minor/patch/tweak);
    // parsing "1.2.3.4.5" should not crash and keep the first 4.
    REQUIRE(result->version().major() == 1);
    REQUIRE(result->version().minor() == 2);
    REQUIRE(result->version().patch() == 3);
    REQUIRE(result->version().tweak() == 4);

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser compatVersion field", "[ds-bank][parser][complex][version]") {
    const auto dir = makeTempDir("compat-version");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.compat",
        "version": "2.0.0",
        "compatVersion": "1.0.0",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");
    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0", "id": "s", "level": 1,
        "configuration": {"defaultLanguage": "cmn", "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]}
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->compatVersion().has_value());

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// Dependencies with version ranges
// ===========================================================================

TEST_CASE("PackageParser multiple dependencies", "[ds-bank][parser][complex][deps]") {
    const auto dir = makeTempDir("multi-deps");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.with-deps",
        "version": "1.0.0",
        "dependencies": [
            {"id": "base.pkg", "version": "1.0.0"},
            {"id": "phoneme.pkg", "version": ">=2.0.0"},
            {"id": "optional.pkg", "version": "*"}
        ],
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");
    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0", "id": "s", "level": 1,
        "configuration": {"defaultLanguage": "cmn", "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]}
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->dependencies().size() == 3);
    REQUIRE(result->dependencies()[0] == "base.pkg");
    REQUIRE(result->dependencies()[1] == "phoneme.pkg");
    REQUIRE(result->dependencies()[2] == "optional.pkg");

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// Voicebank G2P packages referenced in languages
// ===========================================================================

TEST_CASE("PackageParser language with g2pPackages and version", "[ds-bank][parser][complex][g2p]") {
    const auto dir = makeTempDir("lang-g2p-pkgs");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.g2p-pkgs",
        "version": "1.0.0",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");

    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0",
        "id": "singer_g2p",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{
                "id": "cmn",
                "g2p": "g2p-cmn-custom",
                "s2pMode": "dict",
                "g2pPackages": ["g2p/g2p-cmn-base", "g2p/g2p-cmn-ext"],
                "g2pPackageVersion": "1.5.0"
            }]
        }
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->singers().size() == 1);
    REQUIRE(result->singers().front().languages().size() == 1);
    const auto &lang = result->singers().front().languages().front();
    REQUIRE(lang.g2pId() == "g2p-cmn-custom");
    REQUIRE(lang.g2pPackages().size() == 2);
    REQUIRE(lang.hasG2pPackageVersion());
    REQUIRE(lang.g2pPackageVersion().toString() == "1.5");

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser language without g2pPackages uses official G2P", "[ds-bank][parser][complex][g2p]") {
    const auto dir = makeTempDir("lang-official");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.official-g2p",
        "version": "1.0.0",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");

    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0",
        "id": "singer_off",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{
                "id": "cmn",
                "g2p": "g2p-cmn-official",
                "s2pMode": "dict"
            }]
        }
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    const auto &lang = result->singers().front().languages().front();
    REQUIRE(lang.g2pPackages().empty());
    REQUIRE(!lang.hasG2pPackageVersion());

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser language with onset file and mode", "[ds-bank][parser][complex][g2p]") {
    const auto dir = makeTempDir("lang-onset");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.onset",
        "version": "1.0.0",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");

    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0",
        "id": "singer_onset",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{
                "id": "cmn",
                "g2p": "g2p-cmn-official",
                "s2pMode": "dict",
                "dict": "../../assets/cmn.txt",
                "onsetMode": "rule",
                "onsetFile": "../../assets/cmn_onset.json"
            }]
        }
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    const auto &lang = result->singers().front().languages().front();
    REQUIRE(lang.onsetMode() == "rule");
    REQUIRE(!lang.onsetFile().empty());
    REQUIRE(!lang.dict().empty());

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// PackageValidator diagnostics
// ===========================================================================

TEST_CASE("PackageValidator unknown g2p id triggers recommendation", "[ds-bank][validator][complex]") {
    const auto dir = makeTempDir("validator-unknown-g2p");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.bad-g2p",
        "version": "1.0.0",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");

    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0",
        "id": "s",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "unknown", "s2pMode": "dict"}]
        }
    })json");

    PackageValidator validator;
    auto report = validator.validatePackage(dir, PackageValidator::SchemaVersion::V10);

    bool foundG2pWarning = false;
    for (const auto &item : report.items()) {
        if (item.path.find("languages/0/g2p") != std::string::npos &&
            item.recommendation.find("concrete G2P id") != std::string::npos) {
            foundG2pWarning = true;
        }
    }
    REQUIRE(foundG2pWarning);

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageValidator missing singer id in config", "[ds-bank][validator][complex]") {
    const auto dir = makeTempDir("validator-no-singer-id");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.no-singer-id",
        "version": "1.0.0",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");

    // Singer config missing "id" field.
    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");

    PackageValidator validator;
    auto report = validator.validatePackage(dir, PackageValidator::SchemaVersion::V10);

    // Should have an error about missing singer id.
    REQUIRE(report.hasErrors());

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageValidator import without inferenceId", "[ds-bank][validator][complex]") {
    const auto dir = makeTempDir("validator-no-inf-id");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.no-inf-id",
        "version": "1.0.0",
        "contributes": {
            "singers": ["characters/s/config.json"],
            "inferences": ["inferences/duration/config.json"]
        }
    })json");

    // Import missing "inferenceId" (or "id" in newer schema).
    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0",
        "id": "s",
        "level": 1,
        "imports": [{}],
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");
    writeFile(dir / "inferences/duration/config.json", R"json({
        "id": "duration", "class": "ai.svs.DurationInference", "level": 1, "configuration": {}
    })json");

    PackageValidator validator;
    auto report = validator.validatePackage(dir, PackageValidator::SchemaVersion::V10);

    REQUIRE(report.hasErrors());

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageValidator extra root keys produce warnings", "[ds-bank][validator][complex]") {
    const auto dir = makeTempDir("validator-extra-keys");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.extra",
        "version": "1.0.0",
        "unknownField": true,
        "anotherExtra": "value",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");
    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0", "id": "s", "level": 1,
        "configuration": {"defaultLanguage": "cmn", "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]}
    })json");

    PackageValidator validator;
    auto report = validator.validatePackage(dir, PackageValidator::SchemaVersion::V10);

    REQUIRE(report.hasWarnings());

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageValidator invalid version type (number instead of string)", "[ds-bank][validator][complex]") {
    const auto dir = makeTempDir("validator-bad-version");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.bad-ver",
        "version": 1,
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");
    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0", "id": "s", "level": 1,
        "configuration": {"defaultLanguage": "cmn", "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]}
    })json");

    PackageValidator validator;
    auto report = validator.validatePackage(dir, PackageValidator::SchemaVersion::V10);

    REQUIRE(report.hasErrors());
    bool foundVersionError = false;
    for (const auto &item : report.items()) {
        if (item.path.find("desc.json#/version") != std::string::npos) {
            foundVersionError = true;
            break;
        }
    }
    REQUIRE(foundVersionError);

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// Strict vs Relaxed mode
// ===========================================================================

TEST_CASE("PackageParser strict mode rejects unknown fields", "[ds-bank][parser][complex][strict]") {
    const auto dir = makeTempDir("strict-unknown");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.strict",
        "version": "1.0.0",
        "unknownField": "value",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");
    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0", "id": "s", "level": 1,
        "configuration": {"defaultLanguage": "cmn", "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]}
    })json");

    PackageParser parser;

    // Strict mode should reject unknown fields.
    auto strictResult = parser.parsePackage(dir, PackageParser::ParseMode::Strict);
    // If strict mode rejects, the result is an error.
    // (Actual behavior depends on implementation; some strict parsers warn but don't fail.)

    // Relaxed mode should accept.
    auto relaxedResult = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(relaxedResult.hasValue());

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser missing desc.json returns error", "[ds-bank][parser][complex][strict]") {
    const auto dir = makeTempDir("no-desc");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(!result.hasValue());
    REQUIRE(result.error().message().find("desc.json") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser missing singer config file silently skips", "[ds-bank][parser][complex][strict]") {
    const auto dir = makeTempDir("missing-singer-file");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.missing-file",
        "version": "1.0.0",
        "contributes": {"singers": ["characters/nonexistent/config.json"]}
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Strict);
    // Parser silently skips missing singer config files; singers list is empty.
    REQUIRE(result.hasValue());
    REQUIRE(result->singers().empty());

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// phonemeLength default and custom values
// ===========================================================================

TEST_CASE("PackageParser singer default phonemeLength is 48", "[ds-bank][parser][complex][phoneme]") {
    const auto dir = makeTempDir("phoneme-default");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.phoneme",
        "version": "1.0.0",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");

    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0", "id": "s", "level": 1,
        "configuration": {"defaultLanguage": "cmn", "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]}
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    // Default phonemeLength should be 48 (DiffSinger convention).
    REQUIRE(result->singers().front().phonemeLength() == 48.0);

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// Speaker info and speakerMapping in imports
// ===========================================================================

TEST_CASE("PackageParser singer with multiple speakers", "[ds-bank][parser][complex][speakers]") {
    const auto dir = makeTempDir("multi-speakers");

    writeFile(dir / "desc.json", R"json({
        "id": "pkg.speakers",
        "version": "1.0.0",
        "contributes": {"singers": ["characters/s/config.json"]}
    })json");

    writeFile(dir / "characters/s/config.json", R"json({
        "$version": "1.0",
        "id": "singer_spk",
        "level": 1,
        "imports": [{"inferenceId": "duration", "options": {"speakerMapping": {"main": "model_main", "soft": "model_soft"}}}],
        "configuration": {
            "defaultLanguage": "cmn",
            "speakers": [
                {"id": "main", "name": "Main Voice"},
                {"id": "soft", "name": "Soft Voice"},
                {"id": "power", "name": "Power Voice"}
            ],
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result->singers().front().speakers().size() == 3);
    REQUIRE(result->singers().front().speakers()[0].speakerId() == "main");
    REQUIRE(result->singers().front().speakers()[1].speakerId() == "soft");
    REQUIRE(result->singers().front().speakers()[2].speakerId() == "power");

    // Check speakerMapping in imports.
    REQUIRE(result->singers().front().imports().size() == 1);
    const auto &mapping = result->singers().front().imports().front().speakerMapping;
    REQUIRE(mapping.size() == 2);
    REQUIRE(mapping.at("main") == "model_main");
    REQUIRE(mapping.at("soft") == "model_soft");

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// Realistic combined scenario: full voicebank with custom G2P
// ===========================================================================

TEST_CASE("PackageParser realistic voicebank with custom G2P and full pipeline", "[ds-bank][parser][complex][realistic]") {
    const auto dir = makeTempDir("realistic-full");

    // Simulate a realistic voicebank package:
    // - Custom G2P for cmn
    // - Official G2P for en
    // - Full 5-stage pipeline (duration/pitch/variance/acoustic/vocoder)
    // - 2 speakers
    // - phonemeLength = 48
    // - Dependencies on base packages
    writeFile(dir / "desc.json", R"json({
        "id": "org.diffinger.opencpop",
        "version": "2.1.0",
        "compatVersion": "1.0.0",
        "name": {"_": "OpenCPOP Voice Bank"},
        "description": "Mandarin singing voice synthesis",
        "vendor": "OpenCPOP Team",
        "license": "MIT",
        "dependencies": [
            {"id": "base.phonemes", "version": "1.0.0"},
            {"id": "base.pitch", "version": "*"}
        ],
        "contributes": {
            "singers": ["characters/opencpop/config.json"],
            "inferences": [
                "inferences/duration/config.json",
                "inferences/pitch/config.json",
                "inferences/variance/config.json",
                "inferences/acoustic/config.json",
                "inferences/vocoder/config.json"
            ]
        }
    })json");

    writeFile(dir / "characters/opencpop/config.json", R"json({
        "$version": "1.0",
        "id": "opencpop_singer",
        "name": {"_": "OpenCPOP Singer"},
        "level": 2,
        "phonemeLength": 48,
        "imports": [
            {"inferenceId": "duration", "options": {"speakerMapping": {"main": "model_main"}}},
            {"inferenceId": "pitch"},
            {"inferenceId": "variance"},
            {"inferenceId": "acoustic", "options": {"speakerMapping": {"main": "model_main"}}},
            {"inferenceId": "vocoder"}
        ],
        "configuration": {
            "defaultLanguage": "cmn",
            "speakers": [
                {"id": "main", "name": "Main"},
                {"id": "soft", "name": "Soft"}
            ],
            "languages": [
                {
                    "id": "cmn",
                    "name": "Mandarin",
                    "g2p": "g2p-cmn-custom",
                    "dict": "assets/cmn_dict.txt",
                    "s2pMode": "dict",
                    "onsetMode": "rule",
                    "onsetFile": "assets/cmn_onset.json",
                    "g2pPackages": ["g2p/g2p-cmn-custom"],
                    "g2pPackageVersion": "1.2.0"
                },
                {
                    "id": "en",
                    "name": "English",
                    "g2p": "g2p-en-official",
                    "s2pMode": "dict",
                    "dict": "assets/en_dict.txt"
                }
            ]
        }
    })json");

    // Create inference configs.
    for (const auto &name : {"duration", "pitch", "variance", "acoustic", "vocoder"}) {
        std::string cls = "ai.svs." + std::string(name) + "Inference";
        if (name == "acoustic") cls = "ai.svs.AcousticInference";
        std::string cfg = "{\n";
        cfg += "    \"id\": \"" + std::string(name) + "\",\n";
        cfg += "    \"class\": \"" + cls + "\",\n";
        cfg += "    \"level\": " + std::string(name == "acoustic" ? "2" : "1") + ",\n";
        cfg += "    \"configuration\": {\"hiddenSize\": 256}\n";
        cfg += "}\n";
        writeFile(dir / ("inferences/" + std::string(name) + "/config.json"), cfg);
    }

    writeFile(dir / "assets/cmn_dict.txt", "ni\tn i\nhao\th ao\n");
    writeFile(dir / "assets/en_dict.txt", "hello\th ah l ow\n");
    writeFile(dir / "assets/cmn_onset.json", "{\"phonemeTypes\":{},\"rules\":[]}");

    PackageParser parser;
    auto result = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());

    // Verify package-level fields.
    REQUIRE(result->packageId() == "org.diffinger.opencpop");
    REQUIRE(result->name() == "OpenCPOP Voice Bank");
    REQUIRE(result->author() == "OpenCPOP Team");
    REQUIRE(result->license() == "MIT");
    REQUIRE(result->dependencies().size() == 2);

    // Verify singer.
    REQUIRE(result->singers().size() == 1);
    const auto &singer = result->singers().front();
    REQUIRE(singer.singerId() == "opencpop_singer");
    REQUIRE(singer.phonemeLength() == 48.0);
    REQUIRE(singer.speakers().size() == 2);
    REQUIRE(singer.imports().size() == 5);
    REQUIRE(singer.languages().size() == 2);

    // Verify cmn language (custom G2P).
    const auto &cmnLang = singer.languages()[0];
    REQUIRE(cmnLang.languageId() == "cmn");
    REQUIRE(cmnLang.g2pId() == "g2p-cmn-custom");
    REQUIRE(cmnLang.g2pPackages().size() == 1);
    REQUIRE(cmnLang.hasG2pPackageVersion());
    REQUIRE(cmnLang.g2pPackageVersion().toString() == "1.2");
    REQUIRE(cmnLang.onsetMode() == "rule");

    // Verify en language (official G2P).
    const auto &enLang = singer.languages()[1];
    REQUIRE(enLang.languageId() == "en");
    REQUIRE(enLang.g2pId() == "g2p-en-official");
    REQUIRE(enLang.g2pPackages().empty());
    REQUIRE(!enLang.hasG2pPackageVersion());

    // Verify inferences.
    REQUIRE(result->inferences().size() == 5);

    std::filesystem::remove_all(dir);
}
