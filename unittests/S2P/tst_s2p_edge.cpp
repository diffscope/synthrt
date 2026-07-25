// S2P edge-case tests (S2P-001 ~ S2P-012).
//
// Covers edge inputs and error paths of the DictionaryS2P / DirectS2P / MappingS2P public APIs.
// Written based on actual API signatures and implementation behavior; deviations from the
// test matrix are noted in each case's comments.
//
// API difference notes (relative to test matrix):
//   - S2P-001: DictionaryS2P::create with empty stream actually succeeds (empty dictionary),
//     not an error. Corresponds to the "empty dictionary" branch of the matrix entry
//     "Error(InvalidFormat) or empty dictionary".
//   - S2P-002: DictionaryS2P::create with malformed TSV actually returns S2pDictionaryError
//     (matrix describes InvalidFormat). Dictionary parse errors in the S2P error code range
//     are uniformly S2pDictionaryError (see existing contract in tst_s2p_strategies.cpp).
//   - S2P-009: MappingS2P::create with empty stream actually succeeds (empty mapping),
//     not an error. Corresponds to the "empty mapping" branch of the matrix entry
//     "Error(InvalidFormat) or empty mapping".
//   - S2P-010: MappingS2P::convert with unmapped phoneme actually passes through the
//     original phoneme (non-empty). Corresponds to the "original phoneme" branch of the
//     matrix entry "return original phoneme or empty".
//
// P2 strategy: S2P-011 was previously disabled per the "mark P2 cases with SKIP()"
// rule; the test body is now enabled because the no-crash contract and the
// observable BOM-handling behavior (BOM prefix on first key, bare-key miss) are
// both verifiable at L1 without any external dependency.
// S2P-012 was reclassified and now executes directly (DirectS2P::convert is a static
// pure function with no side effects, safe to exercise at L1).

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/S2P/DictionaryS2P.h>
#include <synthrt/S2P/DirectS2P.h>
#include <synthrt/S2P/MappingS2P.h>

using srt::core::ErrorCode;
using srt::s2p::DictionaryS2P;
using srt::s2p::DirectS2P;
using srt::s2p::MappingS2P;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

// ===========================================================================
// S2P-001: DictionaryS2P::create with empty stream
//   Actually succeeds (empty dictionary); querying any pronunciation returns empty vector.
// ===========================================================================
TEST_CASE("S2P-001: DictionaryS2P::create with empty stream", "[s2p][edge]") {
    std::istringstream empty;
    auto result = DictionaryS2P::create(empty);
    REQUIRE(result.hasValue());
    REQUIRE(result->get() != nullptr);
    REQUIRE((*result)->convert("anything").empty());
}

// ===========================================================================
// S2P-002: DictionaryS2P::create with malformed TSV
//   Missing tab separator -> S2pDictionaryError (matrix InvalidFormat; see file header note).
// ===========================================================================
TEST_CASE("S2P-002: DictionaryS2P::create with malformed TSV", "[s2p][edge]") {
    std::istringstream input{"hello world\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pDictionaryError);
}

// ===========================================================================
// S2P-003/004/005: DictionaryS2P::convert miss cases
//   Merged: all three share the same dictionary setup ("hello\th e l l o\n")
//   and verify that convert returns an empty vector for non-matching inputs.
//   SECTION form preserves per-case traceability while removing the
//   triplicated DictionaryS2P::create setup. Adds a known-hit baseline.
// ===========================================================================
TEST_CASE("S2P-003/004/005: DictionaryS2P::convert miss cases", "[s2p][edge]") {
    std::istringstream input{"hello\th e l l o\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(result.hasValue());

    SECTION("S2P-003: unknown pronunciation returns empty") {
        REQUIRE((*result)->convert("nonexistent").empty());
    }

    SECTION("S2P-004: empty string returns empty") {
        REQUIRE((*result)->convert("").empty());
    }

    SECTION("S2P-005: very long string does not crash") {
        const std::string huge(100000, 'a');
        auto out = (*result)->convert(huge);
        REQUIRE(out.empty());
    }

    SECTION("S2P-003b: known hit returns pronunciation") {
        // Baseline: a known-hit query must return the pronunciation so the
        // miss cases above are meaningful (not a broken dictionary).
        auto out = (*result)->convert("hello");
        REQUIRE(out == std::vector<std::string>{"h", "e", "l", "l", "o"});
    }
}

// ===========================================================================
// S2P-006/007/008/012: DirectS2P::convert edge cases
//   Merged: all four exercise the static DirectS2P::convert function with
//   different inputs and share no state. SECTION form preserves per-case
//   traceability and groups the "split-by-space" contract in one place.
//   Adds a multi-space boundary case.
// ===========================================================================
TEST_CASE("S2P-006/007/008/012: DirectS2P::convert edge cases",
          "[s2p][edge]") {
    SECTION("S2P-006: empty string returns empty") {
        REQUIRE(DirectS2P::convert("").empty());
    }

    SECTION("S2P-007: whitespace-only returns empty") {
        REQUIRE(DirectS2P::convert("   ").empty());
    }

    SECTION("S2P-008: Unicode phonemes preserved as single tokens") {
        // Splits only by ASCII space; multibyte UTF-8 phonemes (e.g. "ç")
        // are preserved as a single token.
        auto result = DirectS2P::convert("a b ç");
        REQUIRE(result == std::vector<std::string>{"a", "b", "ç"});
    }

    SECTION("S2P-012: tab/newline are not delimiters") {
        // DirectS2P.cpp only treats ' ' as a delimiter; tab/newline stay
        // inside the token. DirectS2P::convert is a static pure function,
        // safe to exercise at L1.
        auto result = DirectS2P::convert("a\tb\nc");
        REQUIRE(result.size() == 1);
        REQUIRE(result[0] == "a\tb\nc");
    }

    SECTION("S2P-008b: multiple consecutive spaces collapse") {
        // Boundary: runs of spaces between tokens collapse to empty tokens
        // only if the implementation keeps them; verify the actual contract
        // (DirectS2P skips empty tokens between delimiters).
        auto result = DirectS2P::convert("a   b");
        REQUIRE(result == std::vector<std::string>{"a", "b"});
    }
}

// ===========================================================================
// S2P-009/010: MappingS2P::create and convert edge cases
//   Merged: both verify the MappingS2P::create + convert contract. SECTION
//   form preserves per-case traceability while grouping the empty-mapping
//   and unmapped-phoneme behaviors. Adds a known-mapping hit baseline.
// ===========================================================================
TEST_CASE("S2P-009/010: MappingS2P create and convert edge cases",
          "[s2p][edge]") {
    SECTION("S2P-009: empty stream -> pass-through (DirectS2P equivalent)") {
        std::istringstream empty;
        auto result = MappingS2P::create(empty);
        REQUIRE(result.hasValue());
        REQUIRE(result->get() != nullptr);
        REQUIRE((*result)->convert("a b c") ==
                std::vector<std::string>{"a", "b", "c"});
    }

    SECTION("S2P-010: unmapped phoneme passes through original") {
        // Matrix "return original phoneme or empty", original-phoneme branch.
        std::istringstream input{"a\tA\n"};
        auto result = MappingS2P::create(input);
        REQUIRE(result.hasValue());
        auto out = (*result)->convert("unknown_phoneme");
        REQUIRE(out == std::vector<std::string>{"unknown_phoneme"});
    }

    SECTION("S2P-010b: mapped phoneme is translated") {
        // Baseline: a mapped phoneme must be translated so the pass-through
        // case above is meaningful (mapping table actually works).
        std::istringstream input{"a\tA\n"};
        auto result = MappingS2P::create(input);
        REQUIRE(result.hasValue());
        auto out = (*result)->convert("a");
        REQUIRE(out == std::vector<std::string>{"A"});
    }
}

// ===========================================================================
// S2P-011: DictionaryS2P TSV with BOM header (P2)
//   Passing a stream that starts with a UTF-8 BOM does not crash.
//   The implementation does not strip BOM, so the first pronunciation key
//   carries the BOM prefix; querying the bare key misses and returns empty.
//   This is the no-crash contract plus the observable BOM-handling behavior.
// ===========================================================================
TEST_CASE("S2P-011: DictionaryS2P TSV with UTF-8 BOM does not crash",
          "[s2p][edge]") {
    const std::string bom = "\xEF\xBB\xBF";
    std::istringstream input{bom + "hello\th e l l o\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(result.hasValue());
    // BOM prefix makes pronunciation not equal to "hello"; querying "hello" misses and returns empty.
    REQUIRE((*result)->convert("hello").empty());
}

// ===========================================================================
// S2P-013: DirectS2P::convert single-character input
//   A single-character phoneme should be returned as a single token. Covers the splitting
//   contract for the minimal non-empty input (see DirectS2P.cpp: when find(' ') misses,
//   substr takes the whole segment as one token).
// ===========================================================================
TEST_CASE("S2P-013: DirectS2P::convert single character returns single token",
          "[s2p][edge]") {
    auto result = DirectS2P::convert("a");
    REQUIRE(result == std::vector<std::string>{"a"});
}

// ===========================================================================
// S2P-014: DirectS2P::convert collapses consecutive spaces
//   Multiple consecutive ASCII spaces should not produce empty tokens (see DirectS2P.cpp:
//   empty tokens are skipped to tolerate leading/consecutive/trailing spaces). Covers the
//   consecutive-space compression contract.
// ===========================================================================
TEST_CASE("S2P-014: DirectS2P::convert collapses consecutive spaces",
          "[s2p][edge]") {
    auto result = DirectS2P::convert("a   b");
    REQUIRE(result == std::vector<std::string>{"a", "b"});
}

// ===========================================================================
// S2P-015: DirectS2P::convert very long input does not crash
//   A 100k-character string with no spaces should be returned as a single token, without
//   crash or truncation. Covers the large-input robustness contract (symmetric with
//   S2P-005 DictionaryS2P very long input).
// ===========================================================================
TEST_CASE("S2P-015: DirectS2P::convert very long input does not crash",
          "[s2p][edge]") {
    const std::string huge(100000, 'a');
    auto result = DirectS2P::convert(huge);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == huge);
}

// ===========================================================================
// S2P-016: DictionaryS2P::convert is case-sensitive
//   When the dictionary key is "hello", "hello" hits and returns the phoneme list;
//   "Hello"/"HELLO" miss due to case difference and return empty (see DictionaryS2P.cpp:
//   unordered_map::find exact match, no case folding). Covers the case-sensitive contract.
// ===========================================================================
TEST_CASE("S2P-016: DictionaryS2P::convert is case-sensitive", "[s2p][edge]") {
    std::istringstream input{"hello\th e l l o\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(result.hasValue());
    REQUIRE(!(*result)->convert("hello").empty());
    REQUIRE((*result)->convert("Hello").empty());
    REQUIRE((*result)->convert("HELLO").empty());
}

// ===========================================================================
// S2P-017: MappingS2P::create with malformed TSV (missing tab) returns error
//   A line missing the tab separator should return S2pConversionFailed (see MappingS2P.cpp:
//   "missing tab separator"). Note MappingS2P's error code is S2pConversionFailed, different
//   from DictionaryS2P's S2pDictionaryError (S2P-002 contract).
// ===========================================================================
TEST_CASE("S2P-017: MappingS2P::create with malformed TSV returns error",
          "[s2p][edge]") {
    std::istringstream input{"hello world\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(!result.hasValue());
    REQUIRE(result.takeError().code() == ErrorCode::S2pConversionFailed);
}

// ===========================================================================
// S2P-018: MappingS2P::convert empty string returns empty
//   The empty string yields no tokens after DirectS2P splitting, so convert returns an
//   empty vector (see MappingS2P.cpp: convert calls DirectS2P::convert first; empty input
//   yields empty phonemes and returns directly). Covers the empty-input robustness contract.
// ===========================================================================
TEST_CASE("S2P-018: MappingS2P::convert empty string returns empty",
          "[s2p][edge]") {
    std::istringstream input{"a\tA\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(result.hasValue());
    REQUIRE((*result)->convert("").empty());
}

// ===========================================================================
// S2P-019: MappingS2P::convert maps each phoneme independently
//   Input "a b" under mapping a->A, b->B should return {"A","B"} (see MappingS2P.cpp:
//   each phoneme split out by DirectS2P is looked up in the mapping table and replaced
//   independently). Covers the per-phoneme independent mapping contract, in contrast to
//   DictionaryS2P's whole-word lookup.
// ===========================================================================
TEST_CASE("S2P-019: MappingS2P::convert maps each phoneme independently",
          "[s2p][edge]") {
    std::istringstream input{"a\tA\nb\tB\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(result.hasValue());
    auto out = (*result)->convert("a b");
    REQUIRE(out == std::vector<std::string>{"A", "B"});
}

// ===========================================================================
// S2P-020: DictionaryS2P::convert hit returns stored phoneme list
//   The phoneme column of dictionary row "hello\th e l l o" is split by DirectS2P and
//   stored as {"h","e","l","l","o"}; convert("hello") returns this list verbatim on hit
//   (see DictionaryS2P.cpp: create uses DirectS2P::convert to split the phoneme column,
//   convert returns it->second). Covers the forward contract of the hit path.
// ===========================================================================
TEST_CASE("S2P-020: DictionaryS2P::convert hit returns stored phoneme list",
          "[s2p][edge]") {
    std::istringstream input{"hello\th e l l o\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(result.hasValue());
    auto out = (*result)->convert("hello");
    REQUIRE(out == std::vector<std::string>{"h", "e", "l", "l", "o"});
}
