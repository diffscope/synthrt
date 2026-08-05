// A conformance run of srt::JsonValue against nlohmann/json_test_data.
//
// The data is not part of this repository. Clone it and point this program at the checkout:
//
//     git clone https://github.com/nlohmann/json_test_data
//     test_jsonconformance <path-to-json_test_data>
//
// What is checked, suite by suite:
//
//   - Documents every parser must accept parse, and documents every parser must reject do not.
//     The JSONTestSuite \c i_ files are implementation-defined; whatever we do with them is fine
//     as long as we do not crash, so they are counted and reported but never failed.
//   - Serializing an accepted document and parsing the result gives back an equal value, and
//     serializing that again gives byte-identical text.
//   - The round-trip suite is stricter still: its documents are already in the shape our
//     serializer emits, so the text has to come back byte for byte.
//   - CBOR encoded by another implementation decodes to the same value we parse from the JSON
//     beside it, and our own encoding survives its own decoder.
//   - The CBOR fuzzer corpus decodes or reports an error, and does nothing else.

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <synthrt/Support/JSON.h>

namespace fs = std::filesystem;

using srt::JsonValue;

namespace {

    struct Failure {
        std::string file;
        std::string reason;
    };

    struct Report {
        int checked = 0;
        int skipped = 0;
        std::vector<Failure> failures;

        void fail(const fs::path &file, std::string reason) {
            failures.push_back({file.filename().string(), std::move(reason)});
        }
    };

    Report g_report;

    std::string readFile(const fs::path &path, bool *ok = nullptr) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            if (ok)
                *ok = false;
            return {};
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        if (ok)
            *ok = true;
        return content;
    }

    /// The files are listed in whatever order the filesystem hands them over, which makes two runs
    /// hard to compare. Sort them.
    std::vector<fs::path> filesIn(const fs::path &dir, std::string_view suffix = {}) {
        std::vector<fs::path> result;
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file())
                continue;
            const auto name = entry.path().filename().string();
            if (!suffix.empty() &&
                (name.size() < suffix.size() ||
                 name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0))
                continue;
            result.push_back(entry.path());
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    bool startsWith(const std::string &s, std::string_view prefix) {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    /// Whether any double in here is an infinity or a NaN.
    ///
    /// JSON has no way to write one. A parser that accepts \c 1e999 -- and the suite insists we
    /// do -- has to put something in the text when asked for it again, and what every
    /// implementation puts there is \c null. So these documents parse, and serialize, and simply
    /// do not come back as what they were. That is the format, not a defect, and the round-trip
    /// checks skip them on that ground.
    bool hasNonFinite(const JsonValue &value) {
        switch (value.type()) {
            case JsonValue::Double:
                return !std::isfinite(value.toDouble());
            case JsonValue::Array:
                for (const auto &item : value.toArray()) {
                    if (hasNonFinite(item))
                        return true;
                }
                return false;
            case JsonValue::Object:
                for (const auto &item : value.toObject()) {
                    if (hasNonFinite(item.second))
                        return true;
                }
                return false;
            default:
                return false;
        }
    }

    // -----------------------------------------------------------------------------------------
    // Checks
    // -----------------------------------------------------------------------------------------

    /// Everything an accepted document owes us beyond parsing: its serialization parses back to an
    /// equal value, and serializing that gives the same text. Both indented and compact, because
    /// the two take different paths through the serializer.
    void checkSelfConsistency(const fs::path &path, const JsonValue &value) {
        if (hasNonFinite(value)) {
            g_report.skipped++;
            return;
        }

        for (int indent : {-1, 4}) {
            const auto text = value.toJson(indent);

            std::string error;
            const auto reparsed = JsonValue::fromJson(text, false, &error);
            if (!error.empty()) {
                g_report.fail(path, "our own output does not parse (indent " +
                                        std::to_string(indent) + "): " + error);
                continue;
            }
            if (reparsed != value) {
                g_report.fail(path, "value changed across a serialize/parse round trip (indent " +
                                        std::to_string(indent) + ")");
                continue;
            }
            if (reparsed.toJson(indent) != text) {
                g_report.fail(path, "serialization is not stable (indent " + std::to_string(indent) +
                                        ")");
            }
        }
    }

    /// Our CBOR has to survive our own decoder. This says nothing about the encoding being right,
    /// which is what checkCborAgainst() below is for.
    void checkCborSelfConsistency(const fs::path &path, const JsonValue &value) {
        const auto encoded = value.toCbor();

        std::string error;
        const auto decoded = JsonValue::fromCbor(encoded, &error);
        if (!error.empty()) {
            g_report.fail(path, "our own CBOR does not decode: " + error);
            return;
        }
        if (decoded != value) {
            g_report.fail(path, "value changed across a CBOR round trip");
        }
    }

    /// The real cross-check: a sibling .cbor written by nlohmann/json has to decode to the value we
    /// parsed from the JSON.
    void checkCborAgainst(const fs::path &jsonPath, const JsonValue &value) {
        const fs::path cborPath = jsonPath.string() + ".cbor";
        if (!fs::exists(cborPath))
            return;

        // A single byte, 0x81: an array of one, and then nothing. Whatever went wrong when the
        // fixture was written, there is no value in it to compare against.
        if (jsonPath.filename() == "y_number_too_big_neg_int.json") {
            g_report.skipped++;
            return;
        }

        const auto bytes = readFile(cborPath);
        std::string error;
        const auto decoded = JsonValue::fromCbor(
            stdc::array_view<uint8_t>(reinterpret_cast<const uint8_t *>(bytes.data()),
                                      bytes.size()),
            &error);
        g_report.checked++;
        if (!error.empty()) {
            g_report.fail(cborPath, "foreign CBOR does not decode: " + error);
            return;
        }
        if (decoded != value) {
            g_report.fail(cborPath, "foreign CBOR decodes to a different value than the JSON");
        }
    }

    enum class Expectation {
        Accept,
        Reject,
        Either, ///< Implementation-defined. Counted, reported, never failed.
    };

    JsonValue checkParse(const fs::path &path, Expectation expectation, bool ignoreComments = false,
                         int *eitherAccepted = nullptr) {
        bool ok = false;
        const auto content = readFile(path, &ok);
        if (!ok) {
            g_report.fail(path, "cannot be read");
            return {};
        }

        g_report.checked++;

        std::string error;
        auto value = JsonValue::fromJson(content, ignoreComments, &error);

        switch (expectation) {
            case Expectation::Accept:
                if (!error.empty()) {
                    g_report.fail(path, "rejected, but must be accepted: " + error);
                    return {};
                }
                checkSelfConsistency(path, value);
                checkCborSelfConsistency(path, value);
                checkCborAgainst(path, value);
                return value;

            case Expectation::Reject:
                if (error.empty()) {
                    g_report.fail(path, "accepted, but must be rejected");
                }
                return {};

            case Expectation::Either:
                if (error.empty()) {
                    if (eitherAccepted)
                        (*eitherAccepted)++;
                    checkSelfConsistency(path, value);
                    checkCborSelfConsistency(path, value);
                }
                return value;
        }
        return {};
    }

    // -----------------------------------------------------------------------------------------
    // Suites
    // -----------------------------------------------------------------------------------------

    /// JSONTestSuite, in both the layout the repository ships. The prefix on each filename says
    /// what is expected of it.
    void runJsonTestSuite(const fs::path &dir, const char *label) {
        if (!fs::is_directory(dir)) {
            printf("  %-28s skipped, not present\n", label);
            return;
        }

        int accepted = 0, rejected = 0, eitherAccepted = 0, either = 0;
        const auto before = g_report.failures.size();
        for (const auto &path : filesIn(dir, ".json")) {
            const auto name = path.filename().string();
            if (name == "y_string_utf16.json") {
                // UTF-16LE with a byte order mark. The first round of the suite called that a
                // must-accept; the second reclassified it, since RFC 8259 says the text of an
                // exchanged document is UTF-8. We read UTF-8.
                checkParse(path, Expectation::Either, false, &eitherAccepted);
                either++;
            } else if (startsWith(name, "y_")) {
                checkParse(path, Expectation::Accept);
                accepted++;
            } else if (startsWith(name, "n_")) {
                checkParse(path, Expectation::Reject);
                rejected++;
            } else if (startsWith(name, "i_")) {
                checkParse(path, Expectation::Either, false, &eitherAccepted);
                either++;
            }
        }
        printf("  %-28s %d must-accept, %d must-reject, %d free (%d of them accepted) -- %d "
               "failures\n",
               label, accepted, rejected, either, eitherAccepted,
               int(g_report.failures.size() - before));
    }

    /// json.org's originals and the other assorted documents that simply have to parse.
    void runAcceptAll(const fs::path &dir, const char *label) {
        if (!fs::is_directory(dir)) {
            printf("  %-28s skipped, not present\n", label);
            return;
        }

        int count = 0;
        const auto before = g_report.failures.size();
        for (const auto &path : filesIn(dir, ".json")) {
            checkParse(path, Expectation::Accept);
            count++;
        }
        printf("  %-28s %d documents -- %d failures\n", label, count,
               int(g_report.failures.size() - before));
    }

    /// The JSON_checker suite. Its pass/fail split predates RFC 8259 in two places, noted below.
    void runJsonChecker(const fs::path &dir) {
        if (!fs::is_directory(dir)) {
            printf("  %-28s skipped, not present\n", "json_tests");
            return;
        }

        int count = 0;
        const auto before = g_report.failures.size();
        for (const auto &path : filesIn(dir, ".json")) {
            const auto name = path.filename().string();
            if (startsWith(name, "pass")) {
                checkParse(path, Expectation::Accept);
                count++;
            } else if (startsWith(name, "fail")) {
                // fail1: a bare string at the top level, which RFC 8259 allows and the suite,
                //        written against RFC 4627, does not.
                // fail18: nineteen nested arrays, called too deep. Every parser draws that line
                //         somewhere; ours is at 200.
                const auto expectation = (name == "fail1.json" || name == "fail18.json")
                                             ? Expectation::Either
                                             : Expectation::Reject;
                checkParse(path, expectation);
                count++;
            }
        }
        printf("  %-28s %d documents -- %d failures\n", "json_tests", count,
               int(g_report.failures.size() - before));
    }

    /// The round-trip suite. Each document is already written the way our serializer writes, so
    /// the text has to survive parse-then-serialize unchanged.
    void runRoundtrip(const fs::path &dir) {
        if (!fs::is_directory(dir)) {
            printf("  %-28s skipped, not present\n", "json_roundtrip");
            return;
        }

        int count = 0;
        const auto before = g_report.failures.size();
        for (const auto &path : filesIn(dir, ".json")) {
            const auto value = checkParse(path, Expectation::Accept);
            count++;

            // The smallest subnormal. The fixture spells it with thirteen digits; we write the
            // fewest that read back as the same double, which is 5e-324. Both are that number.
            if (path.filename() == "roundtrip28.json") {
                g_report.skipped++;
                continue;
            }

            auto expected = readFile(path);
            while (!expected.empty() && (expected.back() == '\n' || expected.back() == '\r'))
                expected.pop_back();

            const auto actual = value.toJson();
            if (actual != expected) {
                g_report.fail(path, "round trip changed the text: expected " + expected +
                                        ", got " + actual);
            }
        }
        printf("  %-28s %d documents -- %d failures\n", "json_roundtrip", count,
               int(g_report.failures.size() - before));
    }

    /// The CBOR fuzzer corpus. These are not valid CBOR and are not meant to be; the only
    /// requirement is that the decoder reports an error rather than reading past the end of the
    /// buffer or running out of stack.
    void runCborFuzzCorpus(const fs::path &dir) {
        if (!fs::is_directory(dir)) {
            printf("  %-28s skipped, not present\n", "cbor_regression");
            return;
        }

        int count = 0, decoded = 0;
        for (const auto &path : filesIn(dir)) {
            const auto bytes = readFile(path);
            std::string error;
            const auto value = JsonValue::fromCbor(
                stdc::array_view<uint8_t>(reinterpret_cast<const uint8_t *>(bytes.data()),
                                          bytes.size()),
                &error);
            g_report.checked++;
            count++;
            if (error.empty()) {
                decoded++;
                checkCborSelfConsistency(path, value);
            }
        }
        printf("  %-28s %d inputs, %d decoded, none crashed\n", "cbor_regression", count, decoded);
    }

    /// A UTF-8 validator written here rather than borrowed, so that what the parser decides can be
    /// checked against something other than the routine the parser itself calls.
    bool isValidUtf8(std::string_view s) {
        size_t i = 0;
        while (i < s.size()) {
            const auto lead = uint8_t(s[i]);
            int extra;
            char32_t cp;
            if (lead < 0x80) {
                i++;
                continue;
            } else if ((lead & 0xE0) == 0xC0) {
                extra = 1;
                cp = lead & 0x1F;
            } else if ((lead & 0xF0) == 0xE0) {
                extra = 2;
                cp = lead & 0x0F;
            } else if ((lead & 0xF8) == 0xF0) {
                extra = 3;
                cp = lead & 0x07;
            } else {
                return false; // A continuation byte on its own, or a five-byte lead.
            }

            if (i + size_t(extra) >= s.size())
                return false;
            for (int k = 1; k <= extra; ++k) {
                const auto c = uint8_t(s[i + size_t(k)]);
                if ((c & 0xC0) != 0x80)
                    return false;
                cp = (cp << 6) | (c & 0x3F);
            }

            // Overlong forms, surrogates and anything past the last plane are all sequences that
            // decode to something they were not allowed to be spelled as.
            static const char32_t lowest[] = {0, 0x80, 0x800, 0x10000};
            if (cp < lowest[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                return false;

            i += size_t(extra) + 1;
        }
        return true;
    }

    /// Markus Kuhn's decoder stress file, a line at a time, each line wrapped as a JSON string.
    ///
    /// A line that is valid UTF-8 has to come back as itself. A line that is not has to be
    /// rejected. And whatever happens, what the serializer writes has to be valid UTF-8 -- that
    /// one it is never allowed to get wrong, because a value holding a bad string still has to be
    /// writable.
    void runUtf8Stress(const fs::path &dir) {
        const auto path = dir / "UTF-8-test.txt";
        if (!fs::exists(path)) {
            printf("  %-28s skipped, not present\n", "markus_kuhn");
            return;
        }

        const auto content = readFile(path);
        const auto before = g_report.failures.size();
        int lines = 0, accepted = 0;

        size_t start = 0;
        while (start <= content.size()) {
            auto stop = content.find('\n', start);
            if (stop == std::string::npos)
                stop = content.size();
            std::string_view line(content.data() + start, stop - start);
            start = stop + 1;
            if (line.empty())
                continue;
            lines++;

            std::string text = "\"";
            for (char c : line) {
                if (c == '"' || c == '\\') {
                    text += '\\';
                    text += c;
                } else if (uint8_t(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(uint8_t(c)));
                    text += buf;
                } else {
                    text += c;
                }
            }
            text += '"';

            std::string error;
            const auto value = JsonValue::fromJson(text, false, &error);
            g_report.checked++;

            if (error.empty()) {
                accepted++;
                if (!isValidUtf8(line)) {
                    g_report.fail(path, "accepted a line that is not valid UTF-8");
                } else if (value.toStringView() != line) {
                    g_report.fail(path, "a valid line did not survive parsing");
                }
                if (!isValidUtf8(value.toJson())) {
                    g_report.fail(path, "wrote text that is not valid UTF-8");
                }
            } else if (isValidUtf8(line)) {
                g_report.fail(path, "rejected a line that is valid UTF-8: " + error);
            }
        }

        printf("  %-28s %d lines, %d accepted -- %d failures\n", "markus_kuhn", lines, accepted,
               int(g_report.failures.size() - before));
    }

    /// An indefinite-length byte string, 512 bytes of it, and then a fuzzer's tail: a chunk that
    /// is itself an indefinite-length byte string, which RFC 8949 does not allow -- the pieces of
    /// an indefinite-length string are definite-length strings of the same major type. nlohmann,
    /// where the fixture comes from, flattens the nesting anyway. We do not, so the document has
    /// to be rejected, and the check is that it is rejected for that reason and no other.
    ///
    /// The indefinite lengths this file was meant to exercise are covered by the RFC's own test
    /// vectors in test_JSON.cpp.
    void runBinaryData(const fs::path &dir) {
        const auto path = dir / "cbor_binary.cbor";
        if (!fs::exists(path)) {
            printf("  %-28s skipped, not present\n", "binary_data");
            return;
        }

        const auto bytes = readFile(path);
        std::string error;
        JsonValue::fromCbor(stdc::array_view<uint8_t>(
                                reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()),
                            &error);
        g_report.checked++;
        if (error.find("indefinite-length string") == std::string::npos) {
            g_report.fail(path, error.empty()
                                    ? "ill-formed nesting was accepted"
                                    : "rejected for an unexpected reason: " + error);
        }
        printf("  %-28s 1 document -- %d failures\n", "binary_data",
               int(error.find("indefinite-length string") == std::string::npos));
    }

}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-json_test_data>\n", argv[0]);
        return 2;
    }

    const fs::path root = argv[1];
    if (!fs::is_directory(root)) {
        fprintf(stderr, "%s is not a directory\n", argv[1]);
        return 2;
    }

    printf("Running against %s\n\n", root.string().c_str());

    runJsonTestSuite(root / "nst_json_testsuite" / "test_parsing", "nst_json_testsuite");
    runJsonTestSuite(root / "nst_json_testsuite2" / "test_parsing", "nst_json_testsuite2");
    runJsonChecker(root / "json_tests");
    runAcceptAll(root / "json.org", "json.org");
    runAcceptAll(root / "json_testsuite", "json_testsuite");
    runAcceptAll(root / "json_nlohmann_tests", "json_nlohmann_tests");
    runAcceptAll(root / "nativejson-benchmark", "nativejson-benchmark");
    runAcceptAll(root / "jeopardy", "jeopardy");
    runAcceptAll(root / "big-list-of-naughty-strings", "naughty-strings");
    runUtf8Stress(root / "markus_kuhn");
    runRoundtrip(root / "json_roundtrip");
    runCborFuzzCorpus(root / "cbor_regression");
    runBinaryData(root / "binary_data");

    // Every document in regression/ is well formed, broken_file.json included -- what was broken
    // about it was how a stream was being read, not the JSON.
    runAcceptAll(root / "regression", "regression");

    printf("\n%d checks, %d skipped, %zu failures\n", g_report.checked, g_report.skipped,
           g_report.failures.size());
    if (!g_report.failures.empty()) {
        printf("\n");
        for (const auto &failure : g_report.failures) {
            printf("  %-46s %s\n", failure.file.c_str(), failure.reason.c_str());
        }
        return 1;
    }
    return 0;
}
