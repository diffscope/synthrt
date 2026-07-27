// G2P Error migration tests
//
// Verifies that srt::g2p::Error correctly uses srt::core::ErrorCode
// (G2p* codes, 300-399), preserving the suggestion field and the
// G2pSuccess-aware ok() check.
//
// The legacy Type enum and Type-based constructors were removed in
// Level=3 (2026-07-28); these tests now exercise only the ErrorCode-based
// API.

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

// === d. toString ===

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

// === f. _type 字段不被 ErrorCode 覆盖（typeFromCode 映射验证）
//
// ER-02 修复核心：typeFromCode(G2pSuccess) 必须返回 NoError（而非 SessionError），
// 否则 Error(G2pSuccess).ok() 会返回 false，导致成功被误报为错误。
// 这里验证所有 G2P ErrorCode 都能通过 typeFromCode 映射到合理的 base Type：
//   - G2pSuccess → NoError（关键修复点）
//   - 其他 G2P 错误码 → 非 NoError（确保 ok() 返回 false）
//
// 同时验证 g2p::Error 与 core::Error 在同一 ErrorCode 下产生相同的 _type，
// 保证 slicing 安全（g2p::Error 切片到 core::Error 后 ok() 行为一致）。
// ---------------------------------------------------------------------------

TEST_CASE("typeFromCode maps G2pSuccess to NoError (ER-02 regression)",
          "[g2p][error][migration][type-preservation][er-02]") {
    // 关键修复点：G2pSuccess 必须映射到 NoError，否则 ok() 误报为错误
    Error g2pSuccess(ErrorCode::G2pSuccess, "ok");
    srt::core::Error coreSuccess(ErrorCode::G2pSuccess, "ok");

    REQUIRE(g2pSuccess.ok());
    REQUIRE(coreSuccess.ok());
    // _type 字段通过 type() 暴露；两者必须一致
    REQUIRE(g2pSuccess.type() == coreSuccess.type());
    REQUIRE(g2pSuccess.type() == srt::core::Error::NoError);
}

TEST_CASE("typeFromCode maps G2P failure codes to non-NoError",
          "[g2p][error][migration][type-preservation]") {
    SECTION("G2pConfigError maps to non-NoError") {
        Error e(ErrorCode::G2pConfigError, "cfg");
        REQUIRE_FALSE(e.ok());
        REQUIRE(e.type() != srt::core::Error::NoError);
    }
    SECTION("G2pDependencyError maps to non-NoError") {
        Error e(ErrorCode::G2pDependencyError, "dep");
        REQUIRE_FALSE(e.ok());
        REQUIRE(e.type() != srt::core::Error::NoError);
    }
    SECTION("G2pRuntimeError maps to non-NoError") {
        Error e(ErrorCode::G2pRuntimeError, "rt");
        REQUIRE_FALSE(e.ok());
        REQUIRE(e.type() != srt::core::Error::NoError);
    }
    SECTION("G2pAlreadyInitialized maps to non-NoError") {
        Error e(ErrorCode::G2pAlreadyInitialized, "init");
        REQUIRE_FALSE(e.ok());
        REQUIRE(e.type() != srt::core::Error::NoError);
    }
    SECTION("G2pVersionAmbiguous maps to non-NoError") {
        // V3-10 多版本歧义错误码
        Error e(ErrorCode::G2pVersionAmbiguous, "ambiguous");
        REQUIRE_FALSE(e.ok());
        REQUIRE(e.type() != srt::core::Error::NoError);
        REQUIRE(e.code() == ErrorCode::G2pVersionAmbiguous);
    }
}

TEST_CASE("g2p::Error and core::Error produce same _type for same ErrorCode",
          "[g2p][error][migration][type-preservation][slicing]") {
    // 验证 slicing 安全：g2p::Error 切片到 core::Error 后 type() 一致
    const std::vector<ErrorCode> codes = {
        ErrorCode::G2pSuccess,
        ErrorCode::G2pConfigError,
        ErrorCode::G2pFileSystemError,
        ErrorCode::G2pDependencyError,
        ErrorCode::G2pRuntimeError,
        ErrorCode::G2pAlreadyInitialized,
        ErrorCode::G2pVersionAmbiguous,
    };

    for (const auto code : codes) {
        Error g2pErr(code, "msg");
        srt::core::Error coreErr(code, "msg");
        INFO("ErrorCode: " << static_cast<int>(code));
        REQUIRE(g2pErr.type() == coreErr.type());
        REQUIRE(g2pErr.ok() == coreErr.ok());
        REQUIRE(g2pErr.code() == coreErr.code());
    }
}

TEST_CASE("typeFromCode preserves None as NoError",
          "[g2p][error][migration][type-preservation]") {
    // ErrorCode::None 必须映射到 NoError
    Error e(ErrorCode::None, "");
    REQUIRE(e.ok());
    REQUIRE(e.type() == srt::core::Error::NoError);
    REQUIRE(e.code() == ErrorCode::None);
}
