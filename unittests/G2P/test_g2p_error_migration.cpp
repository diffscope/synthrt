// G2P Error migration tests
//
// Verifies that srt::g2p::Error was correctly migrated from the legacy
// Type enum to srt::core::ErrorCode (G2p* codes, 300-399), preserving the
// suggestion field, the G2pSuccess-aware ok() check, and backward
// compatibility with the deprecated Type-based constructors.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/G2P/Support/Error.h>

using namespace srt::g2p;
using srt::core::ErrorCode;

// === a. G2P Type -> ErrorCode mapping (new constructors) ===

TEST_CASE("G2P Error maps ErrorCode via new constructors", "[g2p][error][migration]") {
    SECTION("G2pConfigError") {
        Error e(ErrorCode::G2pConfigError, "cfg");
        REQUIRE(e.code() == ErrorCode::G2pConfigError);
    }
    SECTION("G2pFileSystemError") {
        Error e(ErrorCode::G2pFileSystemError, "fs");
        REQUIRE(e.code() == ErrorCode::G2pFileSystemError);
    }
    SECTION("G2pDependencyError") {
        Error e(ErrorCode::G2pDependencyError, "dep");
        REQUIRE(e.code() == ErrorCode::G2pDependencyError);
    }
    SECTION("G2pRuntimeError") {
        Error e(ErrorCode::G2pRuntimeError, "rt");
        REQUIRE(e.code() == ErrorCode::G2pRuntimeError);
    }
    SECTION("G2pAlreadyInitialized") {
        Error e(ErrorCode::G2pAlreadyInitialized, "init");
        REQUIRE(e.code() == ErrorCode::G2pAlreadyInitialized);
    }
}

// === b. Suggestion field ===

TEST_CASE("G2P Error suggestion field", "[g2p][error][migration]") {
    SECTION("suggestion() returns the suggestion") {
        Error e(ErrorCode::G2pConfigError, "bad config", "check schema");
        REQUIRE(e.suggestion() == "check schema");
    }
    SECTION("hasSuggestion() is true when suggestion is set") {
        Error e(ErrorCode::G2pFileSystemError, "missing", "verify path");
        REQUIRE(e.hasSuggestion());
    }
    SECTION("hasSuggestion() is false when no suggestion") {
        Error e(ErrorCode::G2pRuntimeError, "boom");
        REQUIRE_FALSE(e.hasSuggestion());
        REQUIRE(e.suggestion().empty());
    }
}

// === c. ok() check ===

TEST_CASE("G2P Error ok() checks G2pSuccess", "[g2p][error][migration]") {
    SECTION("G2pSuccess is ok") {
        Error e(ErrorCode::G2pSuccess, "");
        REQUIRE(e.ok());
    }
    SECTION("G2pConfigError is not ok") {
        Error e(ErrorCode::G2pConfigError, "err");
        REQUIRE_FALSE(e.ok());
    }
}

// === d. Backward compatibility with deprecated Type-based constructors ===
//
// The legacy Type enum and Type-based constructors are [[deprecated]]; the
// tests below intentionally exercise them, so deprecation warnings are
// suppressed portably (MSVC C4996 / GCC&Clang -Wdeprecated-declarations).

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

TEST_CASE("G2P Error backward compat with legacy Type", "[g2p][error][migration][compat]") {
    SECTION("ConfigError maps to G2pConfigError") {
        Error e(Error::ConfigError, "cfg");
        REQUIRE(e.code() == ErrorCode::G2pConfigError);
    }
    SECTION("FileSystemError maps to G2pFileSystemError") {
        Error e(Error::FileSystemError, "fs");
        REQUIRE(e.code() == ErrorCode::G2pFileSystemError);
    }
    SECTION("DependencyError maps to G2pDependencyError") {
        Error e(Error::DependencyError, "dep");
        REQUIRE(e.code() == ErrorCode::G2pDependencyError);
    }
}

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

// === e. toString ===

TEST_CASE("G2P Error toString", "[g2p][error][migration]") {
    SECTION("contains code string and message") {
        Error e(ErrorCode::G2pDependencyError, "cycle detected");
        const std::string s = e.toString();
        REQUIRE(s.find("[G2P::DependencyError]") != std::string::npos);
        REQUIRE(s.find("cycle detected") != std::string::npos);
    }
    SECTION("suggestion is exposed via accessor, not via toString") {
        // The base class toString() renders code + message + location + context,
        // but does not include the G2P-specific suggestion field. The suggestion
        // remains accessible through suggestion()/hasSuggestion().
        Error e(ErrorCode::G2pConfigError, "bad config", "check schema");
        const std::string s = e.toString();
        REQUIRE(s.find("[G2P::ConfigError]") != std::string::npos);
        REQUIRE(e.hasSuggestion());
        REQUIRE(e.suggestion() == "check schema");
    }
}
