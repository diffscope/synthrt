// C ABI error handling tests (BF-25 / BF-29).
//
// Verifies:
//   * srt_last_error() / srt_last_error_code() basic behavior
//   * BF-25: G2P errors no longer misclassified as SRT_ERR_FILE_IO
//   * BF-29: srt_last_error() returns toString() (with "[Category::Code]")
//            and srt_last_error_code() returns the mapped srt_error

#include <catch2/catch_test_macros.hpp>

#include <synthrt/C/srt.h>

#include "LastError.h"

#include <synthrt/Core/Support/Error.h>

#include <string>

// ---------------------------------------------------------------------------
// a. srt_last_error / srt_last_error_code basic behavior
// ---------------------------------------------------------------------------

TEST_CASE("srt_last_error/srt_last_error_code: cleared state returns empty and SRT_OK",
          "[c_abi][error]") {
    srt_clear_last_error();
    REQUIRE(std::string(srt_last_error()).empty());
    REQUIRE(srt_last_error_code() == SRT_OK);
}

TEST_CASE("srt_last_error: after error returns toString with [Category::Code]",
          "[c_abi][error]") {
    srt_clear_last_error();
    srt::core::Error err(srt::core::ErrorCode::FileNotFound, "missing.onnx");
    srt::c::detail::mapError(err);

    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    // toString() format: "[Category::Code] message..." — starts with '['.
    REQUIRE(msg.find("[") != std::string::npos);
    REQUIRE(msg.find("FileNotFound") != std::string::npos);
    REQUIRE(msg.find("missing.onnx") != std::string::npos);
}

TEST_CASE("srt_last_error_code: maps FileNotFound to SRT_ERR_NOT_FOUND",
          "[c_abi][error]") {
    srt_clear_last_error();
    srt::core::Error err(srt::core::ErrorCode::FileNotFound, "missing.onnx");
    srt::c::detail::mapError(err);
    REQUIRE(srt_last_error_code() == SRT_ERR_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// b. BF-25 regression: G2P errors no longer misclassified as SRT_ERR_FILE_IO
//
// Previously, G2P DependencyError (Type=3) and RuntimeError (Type=4) collided
// with core::Error::FileNotOpen (=3) and FileDuplicated (=4), both of which
// map to SRT_ERR_FILE_IO. With ErrorCode-based mapping, G2P codes (300-399)
// route through ErrorCategory::G2P and map to SRT_ERR_INIT_FAILED.
// ---------------------------------------------------------------------------

TEST_CASE("BF-25: G2P DependencyError does not map to SRT_ERR_FILE_IO",
          "[c_abi][bf-25]") {
    srt_clear_last_error();
    srt::core::Error err = srt::core::Error::g2pError(
        srt::core::ErrorCode::G2pDependencyError, "g2p dependency missing");
    srt::c::detail::mapError(err);

    srt_error code = srt_last_error_code();
    REQUIRE(code != SRT_ERR_FILE_IO);
    REQUIRE(code == SRT_ERR_INIT_FAILED);
}

TEST_CASE("BF-25: G2P RuntimeError does not map to SRT_ERR_FILE_IO",
          "[c_abi][bf-25]") {
    srt_clear_last_error();
    srt::core::Error err = srt::core::Error::g2pError(
        srt::core::ErrorCode::G2pRuntimeError, "g2p runtime failure");
    srt::c::detail::mapError(err);

    srt_error code = srt_last_error_code();
    REQUIRE(code != SRT_ERR_FILE_IO);
    REQUIRE(code == SRT_ERR_INIT_FAILED);
}

// ---------------------------------------------------------------------------
// c. BF-29 regression: srt_last_error() returns toString() not just message()
//
// Previously, setLastError stored only error.message(), losing the error
// code/category and source location. Now it stores the full toString()
// output: "[Category::Code] message\n  at file:line:func".
// ---------------------------------------------------------------------------

TEST_CASE("BF-29: srt_last_error returns toString() containing bracket not just message",
          "[c_abi][bf-29]") {
    srt_clear_last_error();
    srt::core::Error err(srt::core::ErrorCode::InvalidArgument, "bad argument");
    srt::c::detail::mapError(err);

    std::string msg = srt_last_error();
    // toString() starts with "[Category::Code]" — the bare message() does not.
    REQUIRE(msg.find("[") != std::string::npos);
    REQUIRE(msg.find("InvalidArgument") != std::string::npos);
    // The message text is still present within toString().
    REQUIRE(msg.find("bad argument") != std::string::npos);
}

TEST_CASE("BF-29: srt_last_error_code returns mapped srt_error",
          "[c_abi][bf-29]") {
    srt_clear_last_error();
    srt::core::Error err(srt::core::ErrorCode::InvalidArgument, "bad argument");
    srt::c::detail::mapError(err);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_ARG);
}
