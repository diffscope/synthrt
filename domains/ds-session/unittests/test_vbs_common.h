// test_vbs_common.h
// Shared fixture helpers for the voicebank session hardening test suites.
//
// Extracted from the former test_voicebank_session_hardening.cpp so that the
// hardening regression coverage can be split into multiple themed executables
// (test_vbs_refresh / snapshot / capability / modelset / language) for
// parallel compilation and parallel ctest execution.
//
// Functions live in namespace vbs_test (not an anonymous namespace) so they
// can be referenced from several translation units. Each helper mirrors the
// original fixture semantics from test_voicebank_session.cpp; the helpers
// here intentionally keep the hardening file's singer config format
// (`"id":"<name>"` inside imports) so existing assertions remain valid.

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace vbs_test {

// Create a unique temp directory laid out as `<root>/bank/` and return the
// root path. The stamp avoids collisions when tests run in parallel via
// `ctest -j`.
inline std::filesystem::path makeRoot() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("ds-session-hardening-" + std::to_string(stamp));
    std::filesystem::create_directories(root / "bank");
    return root;
}

// Write `text` to `path`, creating parent directories as needed.
inline void writeFile(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

// Build a minimal valid voicebank package under `<root>/bank/` with one
// singer (`test`) and one stub duration inference. The singer has no real
// ONNX model, so capability analysis treats it as inference-incomplete.
inline void makePackage(const std::filesystem::path &root) {
    const auto bank = root / "bank";
    writeFile(bank / "desc.json", R"({"id":"session.test","version":"1.0.0","contributes":{"singers":["characters/test/config.json"],"inferences":["inferences/duration/config.json"]}})");
    writeFile(bank / "characters/test/config.json", R"({"id":"test","imports":[{"id":"duration"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank / "inferences/duration/config.json", R"({"id":"duration","class":"ai.svs.DurationInference","configuration":{}})");
}

// Create a second valid package (`session.other`) in a sibling directory
// `<root>/bank2/` under the same root, used to exercise change-summary and
// fingerprint deltas when roots are expanded.
inline void makeSecondPackage(const std::filesystem::path &root) {
    const auto bank2 = root / "bank2";
    writeFile(bank2 / "desc.json", R"({"id":"session.other","version":"2.0.0","contributes":{"singers":["characters/other/config.json"],"inferences":["inferences/duration2/config.json"]}})");
    writeFile(bank2 / "characters/other/config.json", R"({"id":"other","imports":[{"id":"duration2"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank2 / "inferences/duration2/config.json", R"({"id":"duration2","class":"ai.svs.DurationInference","configuration":{}})");
}

// Create two packages with the SAME packageId but DIFFERENT versions under
// sibling roots, used to exercise V3-10 multi-version ambiguity at L1.
// The ambiguity check in VoicebankSession::ensureLanguageReady runs against
// snapshot data only (no G2P routing), so L1 can reach it.
inline void makeSamePackageIdTwoVersions(const std::filesystem::path &root) {
    const auto bank1 = root / "bank";
    writeFile(bank1 / "desc.json", R"({"id":"session.dup","version":"1.0.0","contributes":{"singers":["characters/v1/config.json"],"inferences":["inferences/dur1/config.json"]}})");
    writeFile(bank1 / "characters/v1/config.json", R"({"id":"v1","imports":[{"id":"dur1"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank1 / "inferences/dur1/config.json", R"({"id":"dur1","class":"ai.svs.DurationInference","configuration":{}})");

    const auto bank2 = root / "bank2";
    writeFile(bank2 / "desc.json", R"({"id":"session.dup","version":"2.0.0","contributes":{"singers":["characters/v2/config.json"],"inferences":["inferences/dur2/config.json"]}})");
    writeFile(bank2 / "characters/v2/config.json", R"({"id":"v2","imports":[{"id":"dur2"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank2 / "inferences/dur2/config.json", R"({"id":"dur2","class":"ai.svs.DurationInference","configuration":{}})");
}

} // namespace vbs_test
