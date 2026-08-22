#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/path.h>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/PackagePathResolver.h>
#include <diffsinger/Bank/PackageValidator.h>

namespace {

    std::filesystem::path makeTempPackageDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("ds-bank-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    template <typename PathLike>
    bool samePath(const PathLike &actual, const std::filesystem::path &expected) {
        return std::filesystem::path(actual).lexically_normal() == expected.lexically_normal();
    }

}

TEST_CASE("PackageParser requires standard desc manifest", "[ds-bank][package-parser]") {
    const auto dir = makeTempPackageDir("missing-desc");

    ds::bank::PackageParser parser;
    auto result = parser.parsePackage(dir, ds::bank::PackageParser::ParseMode::Relaxed);

    CHECK(!result.hasValue());
    CHECK(result.error().message().find(stdc::path::to_utf8(dir / "desc.json")) != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackagePathResolver rejects absolute and parent escapes", "[ds-bank][path-validation]") {
    const auto dir = makeTempPackageDir("path-resolver");
    writeFile(dir / "assets/model.onnx", "model");

    auto valid = ds::bank::PackagePathResolver::resolve(dir, dir, "assets/model.onnx");
    CHECK(valid.hasValue());
    auto absolute = ds::bank::PackagePathResolver::resolve(dir, dir,
                                                            stdc::path::to_utf8(dir / "assets/model.onnx"));
    CHECK(!absolute.hasValue());
    auto parent = ds::bank::PackagePathResolver::resolve(dir, dir, "../outside.onnx");
    CHECK(!parent.hasValue());

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser and validator reject escaped contributed paths", "[ds-bank][path-validation]") {
    const auto dir = makeTempPackageDir("path-escape");
    writeFile(dir / "desc.json", R"json({
        "id": "escape.pkg",
        "version": "1.0.0",
        "contributes": {"inferences": ["../outside.json"]}
    })json");

    ds::bank::PackageParser parser;
    auto parsed = parser.parsePackage(dir, ds::bank::PackageParser::ParseMode::Strict);
    CHECK(!parsed.hasValue());

    ds::bank::PackageValidator validator;
    auto report = validator.validatePackage(dir, ds::bank::PackageValidator::SchemaVersion::V10);
    REQUIRE(report.hasErrors());
    bool rejected = false;
    for (const auto &item : report.items()) {
        rejected |= item.path.find("contributes/inferences/0") != std::string::npos &&
                    item.message.find("resource path") != std::string::npos;
    }
    CHECK(rejected);

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackagePathResolver rejects symlink and junction escapes", "[ds-bank][path-validation]") {
    const auto dir = makeTempPackageDir("path-link");
    const auto outside = makeTempPackageDir("path-link-outside");
    writeFile(outside / "model.onnx", "model");

    std::error_code ec;
    std::filesystem::create_directory_symlink(outside, dir / "linked", ec);
    if (ec) {
        SUCCEED("Creating symbolic links is unavailable in this test environment");
    } else {
        auto result = ds::bank::PackagePathResolver::resolve(dir, dir, "linked/model.onnx");
        CHECK(!result.hasValue());
    }

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(outside);
}

TEST_CASE("PackageParser records relaxed diagnostics for rejected resource paths",
          "[ds-bank][path-validation]") {
    struct PathCase {
        std::string name;
        std::string configPath;
        std::string config;
        std::string pointer;
    };
    const std::vector<PathCase> cases = {
        {"language-dict", "characters/demo/config.json", R"json({"id":"demo","configuration":{"languages":[{"id":"cmn","g2p":"g2p-cmn","dict":"../../../outside.txt"}]}})json", "configuration/languages/0/dict"},
        {"language-s2p", "characters/demo/config.json", R"json({"id":"demo","configuration":{"languages":[{"id":"cmn","g2p":"g2p-cmn","s2pFile":"../../../outside.txt"}]}})json", "configuration/languages/0/s2pFile"},
        {"language-onset", "characters/demo/config.json", R"json({"id":"demo","configuration":{"languages":[{"id":"cmn","g2p":"g2p-cmn","onsetFile":"../../../outside.txt"}]}})json", "configuration/languages/0/onsetFile"},
        {"inference-model", "inferences/demo/config.json", R"json({"id":"demo","configuration":{"model":"../../../outside.onnx"}})json", "configuration/model"},
        {"inference-phonemes", "inferences/demo/config.json", R"json({"id":"demo","configuration":{"phonemes":"../../../outside.json"}})json", "configuration/phonemes"},
        {"inference-languages", "inferences/demo/config.json", R"json({"id":"demo","configuration":{"languages":"../../../outside.json"}})json", "configuration/languages"},
        {"inference-embedding", "inferences/demo/config.json", R"json({"id":"demo","configuration":{"speakers":{"main":"../../../outside.emb"}}})json", "configuration/speakers/main"},
    };

    for (const auto &test : cases) {
        const auto dir = makeTempPackageDir(test.name);
        const bool singer = test.configPath.starts_with("characters/");
        writeFile(dir / "desc.json", stdc::formatN(
            R"json({"id":"paths.pkg","version":"1.0.0","contributes":{"%1":["%2"]}})json",
            singer ? "singers" : "inferences", test.configPath));
        writeFile(dir / test.configPath, test.config);

        ds::bank::PackageParser parser;
        auto strict = parser.parsePackage(dir, ds::bank::PackageParser::ParseMode::Strict);
        CHECK(!strict.hasValue());

        auto relaxed = parser.parsePackage(dir, ds::bank::PackageParser::ParseMode::Relaxed);
        REQUIRE(relaxed.hasValue());
        REQUIRE(relaxed.value().diagnostics().size() == 1);
        const auto &diagnostic = relaxed.value().diagnostics().front();
        CHECK(diagnostic.code == srt::core::ErrorCode::PackageManifestInvalid);
        CHECK(diagnostic.severity == srt::core::Severity::Error);
        CHECK(diagnostic.location.find(test.configPath) != std::string::npos);
        CHECK(diagnostic.message.find("invalid package resource path") != std::string::npos);

        ds::bank::PackageValidator validator;
        const auto report = validator.validatePackage(dir, ds::bank::PackageValidator::SchemaVersion::V10);
        REQUIRE(report.hasErrors());
        bool reported = false;
        for (const auto &item : report.items()) {
            reported |= item.path.find(test.pointer) != std::string::npos &&
                        item.message == "invalid package resource path";
        }
        CHECK(reported);
        std::filesystem::remove_all(dir);
    }
}

TEST_CASE("PackageValidator reports standard desc diagnostics", "[ds-bank][validator]") {
    const auto dir = makeTempPackageDir("desc-validator");
    writeFile(dir / "desc.json", R"json({
        "id": "demo.pkg",
        "version": 1,
        "extraRoot": true,
        "contributes": {
            "singers": ["characters/demo/config.json"],
            "inferences": ["inferences/duration/config.json"]
        }
    })json");
    writeFile(dir / "characters/demo/config.json", R"json({
        "$version": "1.0",
        "level": 1,
        "imports": [{"inferenceId": "duration"}],
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "unknown", "s2pMode": "dict"}]
        }
    })json");
    writeFile(dir / "inferences/duration/config.json", R"json({
        "id": "duration",
        "level": 1,
        "configuration": {}
    })json");

    ds::bank::PackageValidator validator;
    auto report = validator.validatePackage(dir, ds::bank::PackageValidator::SchemaVersion::V10);

    CHECK(report.hasErrors());
    bool hasVersionType = false;
    bool hasExtraRoot = false;
    bool hasImportExtra = false;
    bool hasUnknownG2p = false;
    for (const auto &item : report.items()) {
        hasVersionType |= item.path.find("desc.json#/version") != std::string::npos &&
                          item.message.find("invalid value type") != std::string::npos &&
                          item.actualValue.find("number") != std::string::npos;
        hasExtraRoot |= item.path.find("desc.json#/extraRoot") != std::string::npos &&
                        item.severity == ds::bank::ValidationItem::Warning;
        hasImportExtra |= item.path.find("characters/demo/config.json#/imports/0/inferenceId") !=
                          std::string::npos;
        hasUnknownG2p |= item.path.find("characters/demo/config.json#/configuration/languages/0/g2p") !=
                         std::string::npos &&
                         item.recommendation.find("concrete G2P id") != std::string::npos;
    }
    CHECK(hasVersionType);
    CHECK(hasExtraRoot);
    CHECK(hasImportExtra);
    CHECK(hasUnknownG2p);

    std::filesystem::remove_all(dir);
}

TEST_CASE("PackageParser reads standard desc contributes", "[ds-bank][package-parser]") {
    const auto dir = makeTempPackageDir("standard-desc");
    writeFile(dir / "desc.json", R"json({
        "id": "std.pkg",
        "version": "1.2.3",
        "name": {"_": "Standard Package"},
        "contributes": {
            "singers": ["characters/std/config.json"],
            "inferences": ["inferences/duration/config.json"]
        },
        "dependencies": [{"id": "base.pkg", "version": "1.0.0"}]
    })json");
    writeFile(dir / "characters/std/config.json", R"json({
        "$version": "1.0",
        "id": "std_singer",
        "level": 1,
        "name": {"_": "Std Singer"},
        "imports": [{
            "id": "duration",
            "options": {"speakerMapping": {"main": "model_main"}}
        }],
        "configuration": {
            "defaultLanguage": "cmn",
            "speakers": [{"id": "main", "name": "Main"}],
            "languages": [{
                "id": "cmn",
                "name": "Mandarin",
                "g2p": "g2p-cmn-official",
                "dict": "../../assets/cmn.txt",
                "s2pMode": "dict",
                "onsetMode": "rule",
                "onsetFile": "../../assets/cmn_onset.json"
            }]
        }
    })json");
    writeFile(dir / "inferences/duration/config.json", R"json({
        "id":"duration",
        "class":"ai.svs.DurationInference",
        "level":1,
        "configuration": {"phonemes": "phonemes.json"}
    })json");
    writeFile(dir / "inferences/duration/phonemes.json", "{}");
    writeFile(dir / "assets/cmn.txt", "ni\tn i\n");
    writeFile(dir / "assets/cmn_onset.json", "{\"phonemeTypes\":{},\"rules\":[]}");

    ds::bank::PackageParser parser;
    auto result = parser.parsePackage(dir, ds::bank::PackageParser::ParseMode::Strict);

    REQUIRE(result.hasValue());
    const auto &info = result.value();
    CHECK(info.packageId() == "std.pkg");
    CHECK(info.version().toString() == "1.2.3");
    CHECK(info.name().text() == "Standard Package");
    REQUIRE(info.singerRefs().size() == 1);
    REQUIRE(info.inferenceRefs().size() == 1);
    CHECK(samePath(info.singerRefs().front(), dir / "characters/std/config.json"));
    CHECK(samePath(info.inferenceRefs().front(), dir / "inferences/duration/config.json"));
    REQUIRE(info.singers().size() == 1);
    CHECK(info.singers().front().singerId() == "std_singer");
    CHECK(info.singers().front().defaultLanguage() == "cmn");
    REQUIRE(info.singers().front().imports().size() == 1);
    CHECK(info.singers().front().imports().front().inferenceId == "duration");
    CHECK(info.singers().front().imports().front().speakerMapping.at("main") == "model_main");
    REQUIRE(info.inferences().size() == 1);
    CHECK(info.inferences().front().id == "duration");
    CHECK(info.inferences().front().className == "ai.svs.DurationInference");
    CHECK(info.inferences().front().level == 1);
    REQUIRE(info.inferences().front().resourcePaths.size() == 1);
    CHECK(samePath(info.inferences().front().resourcePaths.front(), dir / "inferences/duration/phonemes.json"));
    REQUIRE(info.languages().size() == 1);
    CHECK(info.languages().front().languageId() == "cmn");
    CHECK(samePath(info.languages().front().dict(), dir / "assets/cmn.txt"));
    REQUIRE(info.dependencies().size() == 1);
    CHECK(info.dependencies().front() == "base.pkg");

    std::filesystem::remove_all(dir);
}
