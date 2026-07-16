// Regression tests for v3 error-reporting fixes (ER-02 G2pSuccess slicing,
// ER-01 withTrace/withContext chaining).
//
// Covers 07-test-matrix.md:
//   §1.2 ER-02: G2pSuccess slicing bug regression
//   §1.1 ER-01: appendTrace / withTrace / withContext chainable helpers
//
// The G2pSuccess slicing bug: before the fix, typeFromCode(G2pSuccess) returned
// SessionError (not NoError), so Error(G2pSuccess).ok() was false. When a
// g2p::Error(G2pSuccess) was sliced to core::Error, ok() returned false,
// causing success to be misreported as an error. The fix maps G2pSuccess to
// NoError in typeFromCode (Error.cpp).

#include <source_location>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Support/Error.h>

using namespace srt::core;

// ---------------------------------------------------------------------------
// ER-02: G2pSuccess slicing regression (core::Error base class)
// ---------------------------------------------------------------------------

TEST_CASE("Error(G2pSuccess) is ok via core::Error base", "[error][er-02][g2psuccess]") {
    // Core fix: typeFromCode(G2pSuccess) == NoError, so ok() is true.
    Error e(ErrorCode::G2pSuccess, "ok");
    REQUIRE(e.ok());
    REQUIRE(e.type() == Error::NoError);
    // The diagnostic code is preserved as G2pSuccess (not downgraded to None).
    REQUIRE(e.code() == ErrorCode::G2pSuccess);
    REQUIRE(e.category() == ErrorCategory::G2P);
}

TEST_CASE("Error(G2pSuccess) toString is empty when ok", "[error][er-02][g2psuccess]") {
    // toString() returns {} for ok errors (ok() short-circuits).
    Error e(ErrorCode::G2pSuccess, "ok");
    REQUIRE(e.ok());
    REQUIRE(e.toString().empty());
}

TEST_CASE("G2pSuccess slicing from g2p::Error to core::Error keeps ok true",
          "[error][er-02][g2psuccess][slicing]") {
    // Key regression point: g2p::Error derives from core::Error. When sliced
    // to the base, the base ok() (which checks _type) must return true.
    // Before the fix, _type was SessionError so sliced.ok() was false.
    srt::g2p::Error g2pErr(ErrorCode::G2pSuccess, "ok");
    srt::core::Error sliced = g2pErr; // slicing: only base part copied
    REQUIRE(sliced.ok());
    REQUIRE(sliced.type() == Error::NoError);
    REQUIRE(sliced.code() == ErrorCode::G2pSuccess);
}

TEST_CASE("G2pSuccess does not produce an error Expected when used as success",
          "[error][er-02][g2psuccess][expected]") {
    // The fix ensures Error(G2pSuccess).ok() == true. Code that guards
    // `if (!err.ok()) return Expected<T>(err);` will NOT construct an error
    // Expected from a G2pSuccess. Verify the guard condition holds so the
    // error-Expected path is never taken for success.
    Error err(ErrorCode::G2pSuccess, "ok");
    REQUIRE(err.ok());
    // Because ok() is true, the error-Expected construction is skipped.
    // (Constructing Expected<T> from an ok Error triggers an assert in debug
    // builds and is intentionally avoided here.)
    Expected<int> successExp(42);
    REQUIRE(successExp.hasValue());
    REQUIRE(*successExp == 42);
}

TEST_CASE("G2P failure codes are still errors after the fix",
          "[error][er-02][g2psuccess]") {
    // Ensure the G2pSuccess fix did not affect actual G2P error codes.
    Error e(ErrorCode::G2pConfigError, "bad config");
    REQUIRE(!e.ok());
    REQUIRE(e.code() == ErrorCode::G2pConfigError);
    REQUIRE(e.type() == Error::InvalidFormat);
}

// ---------------------------------------------------------------------------
// ER-01: withTrace / withContext chainable helpers
// ---------------------------------------------------------------------------

TEST_CASE("withTrace appends a trace frame and returns *this", "[error][er-01][trace]") {
    Error err(ErrorCode::InvalidArgument, "bad arg");
    REQUIRE(err.diagnostic().trace.empty());

    auto &ref = err.withTrace(std::source_location::current(), "upper layer");

    // Returns reference to same error (chainable).
    REQUIRE(&ref == &err);
    REQUIRE(err.diagnostic().trace.size() == 1);
    REQUIRE(err.diagnostic().trace[0].find("upper layer") != std::string::npos);
    // Trace entry embeds source location "file:line:function".
    REQUIRE(err.diagnostic().trace[0].find(':') != std::string::npos);
}

TEST_CASE("withContext fills non-empty fields and returns *this",
          "[error][er-01][context]") {
    Error err(ErrorCode::InferenceModelLoadFailed, "load failed");

    auto &ref = err.withContext("singer-A", "acoustic", "pkg-1@1.0.0", "zh");

    REQUIRE(&ref == &err);
    REQUIRE(err.diagnostic().singerId == "singer-A");
    REQUIRE(err.diagnostic().moduleId == "acoustic");
    REQUIRE(err.diagnostic().packageId == "pkg-1@1.0.0");
    REQUIRE(err.diagnostic().language == "zh");
}

TEST_CASE("withContext empty strings are not assigned", "[error][er-01][context]") {
    Error err(ErrorCode::InvalidArgument, "msg");
    // First set singerId via withContext.
    err.withContext("singer-A", {}, {}, {});
    REQUIRE(err.diagnostic().singerId == "singer-A");
    // Call withContext again with empty singerId; it must not clear the field.
    err.withContext({}, "module-only", {}, {});

    REQUIRE(err.diagnostic().singerId == "singer-A"); // untouched by empty
    REQUIRE(err.diagnostic().moduleId == "module-only");
    REQUIRE(err.diagnostic().packageId.empty());
    REQUIRE(err.diagnostic().language.empty());
}

TEST_CASE("withTrace and withContext chain produces toString with trace and context",
          "[error][er-01][trace][context]") {
    Error err(ErrorCode::InferenceModelLoadFailed, "load failed");
    err.withTrace(std::source_location::current(), "ModelSet::load(acoustic)")
        .withContext("singer-A", "acoustic", "pkg-1@1.0.0", "zh");

    auto s = err.toString();
    REQUIRE(s.find("[Inference::ModelLoadFailed]") != std::string::npos);
    REQUIRE(s.find("load failed") != std::string::npos);
    // Context fields appear in toString.
    REQUIRE(s.find("singerId:") != std::string::npos);
    REQUIRE(s.find("singer-A") != std::string::npos);
    REQUIRE(s.find("moduleId:") != std::string::npos);
    REQUIRE(s.find("acoustic") != std::string::npos);
    REQUIRE(s.find("packageId:") != std::string::npos);
    REQUIRE(s.find("pkg-1@1.0.0") != std::string::npos);
    REQUIRE(s.find("language:") != std::string::npos);
    REQUIRE(s.find("zh") != std::string::npos);
    // Trace section appears with the frame note.
    REQUIRE(s.find("trace:") != std::string::npos);
    REQUIRE(s.find("ModelSet::load(acoustic)") != std::string::npos);
}

TEST_CASE("withContext with all empty produces no context line in toString",
          "[error][er-01][context]") {
    Error err(ErrorCode::InvalidArgument, "msg");
    err.withContext({}, {}, {}, {});

    auto s = err.toString();
    // No context field labels should appear.
    REQUIRE(s.find("singerId:") == std::string::npos);
    REQUIRE(s.find("moduleId:") == std::string::npos);
    REQUIRE(s.find("packageId:") == std::string::npos);
    REQUIRE(s.find("language:") == std::string::npos);
}
