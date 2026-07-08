// P1.1 unit tests for srt::core::Error and srt::core::Expected
//
// Verifies that Error/Expected were correctly migrated from srt:: to
// srt::core:: with identical semantics.

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

using namespace srt::core;

TEST_CASE("Error default constructs as NoError", "[p1.1][error]") {
    Error e;
    REQUIRE(e.ok());
    REQUIRE(e.type() == Error::NoError);
    REQUIRE(e.message().empty());
}

TEST_CASE("Error type constructors", "[p1.1][error]") {
    Error e(Error::FileNotFound);
    REQUIRE(!e.ok());
    REQUIRE(e.type() == Error::FileNotFound);
    REQUIRE(e.message() == "file not found");
}

TEST_CASE("Error custom message", "[p1.1][error]") {
    Error e(Error::InvalidArgument, "custom reason");
    REQUIRE(!e.ok());
    REQUIRE(e.type() == Error::InvalidArgument);
    REQUIRE(e.message() == "custom reason");
    REQUIRE(std::string(e.what()) == "custom reason");
}

TEST_CASE("Error success factory", "[p1.1][error]") {
    auto e = Error::success();
    REQUIRE(e.ok());
}

TEST_CASE("Error exposes diagnostic codes", "[p3][diagnostic]") {
    Error e(Error::InvalidArgument, "custom reason");
    REQUIRE(e.code() == ErrorCode::InvalidArgument);
    REQUIRE(e.diagnostic().message == "custom reason");

    Error diagnosticError(Diagnostic{
        ErrorCode::PackageScanAfterInitialize,
        Severity::Error,
        "package sources are immutable",
        "runtime",
    });
    REQUIRE(diagnosticError.type() == Error::InvalidArgument);
    REQUIRE(diagnosticError.code() == ErrorCode::PackageScanAfterInitialize);
    REQUIRE(diagnosticError.diagnostic().location == "runtime");
}

TEST_CASE("Expected<T> value", "[p1.1][expected]") {
    Expected<int> ex(42);
    REQUIRE(static_cast<bool>(ex));
    REQUIRE(ex.hasValue());
    REQUIRE(*ex == 42);
    REQUIRE(ex.value() == 42);
}

TEST_CASE("Expected<T> error", "[p1.1][expected]") {
    Expected<int> ex{Error(Error::FileNotFound)};
    REQUIRE(!static_cast<bool>(ex));
    REQUIRE(!ex.hasValue());
    REQUIRE(ex.error().type() == Error::FileNotFound);
    REQUIRE(ex.error().code() == ErrorCode::FileNotFound);
    auto err = ex.takeError();
    REQUIRE(err.type() == Error::FileNotFound);
}

TEST_CASE("Expected<T> valueOr", "[p1.1][expected]") {
    Expected<int> err{Error(Error::InvalidArgument)};
    REQUIRE(err.valueOr(99) == 99);

    Expected<int> ok(7);
    REQUIRE(ok.valueOr(99) == 7);
}

TEST_CASE("Expected<void>", "[p1.1][expected]") {
    Expected<void> ok;
    REQUIRE(static_cast<bool>(ok));
    REQUIRE(ok.hasValue());

    Expected<void> err{Error(Error::SessionError)};
    REQUIRE(!static_cast<bool>(err));
    REQUIRE(!err.hasValue());
    REQUIRE(err.error().type() == Error::SessionError);
}

TEST_CASE("Expected move semantics", "[p1.1][expected]") {
    Expected<std::string> src(std::string("hello"));
    Expected<std::string> dst(std::move(src));
    REQUIRE(*dst == "hello");
}
