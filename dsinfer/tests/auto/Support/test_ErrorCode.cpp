#include <system_error>

#include <synthrt/Support/Error.h>

#include <dsinfer/Support/ErrorCode.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using ds::ErrorCode;
using srt::Error;

BOOST_AUTO_TEST_SUITE(test_ErrorCode)

BOOST_AUTO_TEST_CASE(test_ErrorCode_Category) {
    BOOST_CHECK(std::string(ds::error_category().name()) == "dsinfer");

    // Every code says something of its own, and none of them borrows the unknown text.
    const ErrorCode codes[] = {
        ErrorCode::NotInitialized, ErrorCode::AlreadyOpen,   ErrorCode::DriverMismatch,
        ErrorCode::DriverLoadFailed, ErrorCode::InvalidInput, ErrorCode::ShapeMismatch,
        ErrorCode::SessionFailed,  ErrorCode::ProcessingFailed,
    };
    for (auto code : codes) {
        const auto ec = make_error_code(code);
        BOOST_CHECK(ec.category() == ds::error_category());
        BOOST_CHECK(!ec.message().empty());
        BOOST_CHECK(ec.message() != "unknown error");
        BOOST_CHECK(static_cast<bool>(ec));
    }

    // A value outside the enum still answers, rather than reading off the end of a table.
    BOOST_CHECK(ds::error_category().message(9999) == "unknown error");
}

// The point of dsinfer having a domain: its codes cannot be mistaken for synthrt's, even where the
// numbers coincide.
BOOST_AUTO_TEST_CASE(test_ErrorCode_DoesNotCollideWithSynthrt) {
    const auto ours = make_error_code(ErrorCode::NotInitialized);
    const auto theirs = std::error_code(Error::InvalidFormat);

    BOOST_REQUIRE(ours.value() == theirs.value()); // both 1, which is the whole point
    BOOST_CHECK(ours != theirs);
    BOOST_CHECK(ours.category() != theirs.category());

    BOOST_CHECK(ours != Error::InvalidFormat);
    BOOST_CHECK(theirs != ErrorCode::NotInitialized);
}

// An ErrorCode reaches a caller inside a srt::Error, which is how the interpreters report.
BOOST_AUTO_TEST_CASE(test_ErrorCode_CarriedBySynthrtError) {
    Error e(ErrorCode::ShapeMismatch, "pitch tensor element count does not match target length");

    BOOST_CHECK(!e.ok());
    BOOST_CHECK(e.code() == ErrorCode::ShapeMismatch);
    BOOST_CHECK(e.code() != ErrorCode::InvalidInput);
    BOOST_CHECK(e.code().category() == ds::error_category());
    BOOST_CHECK(e.message() == "pitch tensor element count does not match target length");

    // Without a message of its own it takes the category's.
    Error bare{std::error_code(ErrorCode::SessionFailed)};
    BOOST_CHECK(bare.message() == "session failed");

    // And it chains like any other, keeping its domain at the root.
    Error wrapped =
        Error(Error::InvalidFormat, "cannot run acoustic inference").withCause(std::move(e));
    BOOST_CHECK(wrapped.code() == Error::InvalidFormat);
    BOOST_CHECK(wrapped.rootCause().code() == ErrorCode::ShapeMismatch);
    BOOST_CHECK(wrapped.toString() ==
                "cannot run acoustic inference: pitch tensor element count does not match "
                "target length");
}

BOOST_AUTO_TEST_SUITE_END()
