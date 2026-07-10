// P5.1 unit tests for the srt v4 TLS error buffer and ABI version.
//
// Verifies:
//   * srt_get_v4_api_version() matches the SRT_V4_API_VERSION macro
//   * srt_last_error() is initially an empty string
//   * srt::c::detail::setLastError() / srt_last_error() round-trip
//   * srt::c::detail::setLastError(Error) maps NoError → cleared
//   * srt_clear_last_error() clears the buffer
//   * srt_free_string(NULL) / srt_free_string_array(NULL, 0) are no-ops
//   * srt_free_string / srt_free_string_array release allocated strings

#include <catch2/catch_test_macros.hpp>

#include <synthrt/C/srt.h>

#include "LastError.h"

#include <synthrt/Core/Support/Error.h>

#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// ABI version
// ---------------------------------------------------------------------------
TEST_CASE("srt_get_v4_api_version matches macro", "[p5.1][version]") {
    REQUIRE(srt_get_v4_api_version() == SRT_V4_API_VERSION);
    REQUIRE(SRT_V4_API_VERSION_MAJOR == 1);
}

// ---------------------------------------------------------------------------
// TLS error buffer
// ---------------------------------------------------------------------------
TEST_CASE("srt_last_error is empty initially", "[p5.1][error]") {
    srt_clear_last_error();
    REQUIRE(std::string(srt_last_error()).empty());
}

TEST_CASE("setLastError(string) round-trips", "[p5.1][error]") {
    srt::c::detail::setLastError("something went wrong");
    REQUIRE(std::string(srt_last_error()) == "something went wrong");

    srt::c::detail::setLastError("");
    REQUIRE(std::string(srt_last_error()).empty());
}

TEST_CASE("setLastError(Error) maps NoError to cleared", "[p5.1][error]") {
    srt::c::detail::setLastError("pending");
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt::core::Error ok;
    srt::c::detail::setLastError(ok);
    REQUIRE(std::string(srt_last_error()).empty());
}

TEST_CASE("setLastError(Error) stores full toString()", "[p5.1][error]") {
    srt::core::Error err(srt::core::Error::FileNotFound, "missing.onnx");
    srt::c::detail::setLastError(err);
    std::string msg = srt_last_error();
    // BF-29: setLastError stores toString() (includes code string + message),
    // not just the bare message.
    REQUIRE(msg.find("missing.onnx") != std::string::npos);
    REQUIRE(msg.find("FileNotFound") != std::string::npos);
}

TEST_CASE("srt_clear_last_error clears buffer", "[p5.1][error]") {
    srt::c::detail::setLastError("transient");
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_clear_last_error();
    REQUIRE(std::string(srt_last_error()).empty());
}

// ---------------------------------------------------------------------------
// String ownership helpers
// ---------------------------------------------------------------------------
TEST_CASE("srt_free_string accepts NULL", "[p5.1][memory]") {
    srt_free_string(nullptr);
    SUCCEED();
}

TEST_CASE("srt_free_string_array accepts NULL", "[p5.1][memory]") {
    srt_free_string_array(nullptr, 0);
    SUCCEED();
}

TEST_CASE("srt_free_string releases allocated string", "[p5.1][memory]") {
    char *s = static_cast<char *>(std::malloc(5));
    REQUIRE(s != nullptr);
    std::memcpy(s, "test", 5);
    srt_free_string(s);
    SUCCEED();
}

TEST_CASE("srt_free_string_array releases array", "[p5.1][memory]") {
    char **arr = static_cast<char **>(std::malloc(2 * sizeof(char *)));
    REQUIRE(arr != nullptr);
    arr[0] = static_cast<char *>(std::malloc(2));
    std::memcpy(arr[0], "a", 2);
    arr[1] = static_cast<char *>(std::malloc(2));
    std::memcpy(arr[1], "b", 2);

    srt_free_string_array(arr, 2);
    SUCCEED();
}
