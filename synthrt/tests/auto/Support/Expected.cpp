#include <synthrt/Support/Expected.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_Expected)

using srt::Error;
using srt::Expected;

BOOST_AUTO_TEST_CASE(test_Expected) {
    // Construct from error
    {
        Expected<std::string> e((Error(Error::InvalidArgument)));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::InvalidArgument);
        BOOST_CHECK(e.error().message() == Error(Error::InvalidArgument).message());
    }
    // Construct from value
    {
        Expected<std::string> e("hello");
        BOOST_CHECK(e.hasValue());
        BOOST_CHECK(e.get() == "hello");
    }
    // Construct from convertible Expected
    {
        Expected<std::string> e1 = Expected<const char *>(Error(Error::InvalidArgument));
        BOOST_CHECK(!e1.hasValue());
        BOOST_CHECK(e1.error().type() == Error::InvalidArgument);

        Expected<std::string> e2 = Expected<const char *>("hello");
        BOOST_CHECK(e2.hasValue());
        BOOST_CHECK(e2.get() == "hello");
    }
}

// operator bool() used to be non-const, so a const Expected could not be tested at all.
BOOST_AUTO_TEST_CASE(test_Expected_ConstContextualConversion) {
    const Expected<std::string> value("hello");
    const Expected<std::string> error((Error(Error::InvalidArgument)));

    BOOST_CHECK(static_cast<bool>(value));
    BOOST_CHECK(!static_cast<bool>(error));

    // The shape that failed to compile before: binding to a const reference and testing it.
    const auto &ref = value;
    BOOST_CHECK(ref ? true : false);

    const Expected<void> voidValue;
    const Expected<void> voidError((Error(Error::InvalidArgument)));
    BOOST_CHECK(static_cast<bool>(voidValue));
    BOOST_CHECK(!static_cast<bool>(voidError));
}

// valueOr's const overload took `const U &` and then applied std::forward<U>, which casts away
// constness; the overload never compiled and so was never instantiated by anything.
BOOST_AUTO_TEST_CASE(test_Expected_ValueOr) {
    {
        const Expected<std::string> value("hello");
        const Expected<std::string> error((Error(Error::InvalidArgument)));

        BOOST_CHECK(value.valueOr("fallback") == "hello");
        BOOST_CHECK(error.valueOr("fallback") == "fallback");

        // A named lvalue is the case that used to fail.
        const std::string fallback = "fallback";
        BOOST_CHECK(value.valueOr(fallback) == "hello");
        BOOST_CHECK(error.valueOr(fallback) == "fallback");
    }
    // Rvalue overload moves out of the contained value.
    {
        BOOST_CHECK(Expected<std::string>("hello").valueOr("fallback") == "hello");
        BOOST_CHECK(Expected<std::string>(Error(Error::InvalidArgument)).valueOr("fallback") ==
                    "fallback");
    }
}

BOOST_AUTO_TEST_CASE(test_Expected_MoveAssign) {
    // Every combination of source and destination state, including the ones that change state.
    {
        Expected<std::string> e("hello");
        e = Expected<std::string>("world");
        BOOST_CHECK(e.hasValue());
        BOOST_CHECK(e.get() == "world");
    }
    {
        Expected<std::string> e("hello");
        e = Expected<std::string>(Error(Error::NotImplemented));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::NotImplemented);
    }
    {
        Expected<std::string> e((Error(Error::InvalidArgument)));
        e = Expected<std::string>("hello");
        BOOST_CHECK(e.hasValue());
        BOOST_CHECK(e.get() == "hello");
    }
    {
        Expected<std::string> e((Error(Error::InvalidArgument)));
        e = Expected<std::string>(Error(Error::NotImplemented));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::NotImplemented);
    }
    {
        Expected<void> e;
        e = Expected<void>(Error(Error::NotImplemented));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::NotImplemented);
    }
}

BOOST_AUTO_TEST_CASE(test_Expected_Void) {
    // Construct from error
    {
        Expected<void> e((Error(Error::InvalidArgument)));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::InvalidArgument);
        BOOST_CHECK(e.error().message() == Error(Error::InvalidArgument).message());
    }
    // Construct from convertible Expected
    {
        Expected<void> e1 = Expected<const char *>(Error(Error::InvalidArgument));
        BOOST_CHECK(!e1.hasValue());
        BOOST_CHECK(e1.error().type() == Error::InvalidArgument);

        Expected<void> e2 = Expected<const char *>("hello");
        BOOST_CHECK(e2.hasValue());
    }
}

BOOST_AUTO_TEST_SUITE_END()