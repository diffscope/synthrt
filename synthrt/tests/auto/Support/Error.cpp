#include <synthrt/Support/Error.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_Error)

using srt::Error;

BOOST_AUTO_TEST_CASE(test_Error_Basic) {
    {
        Error e;
        BOOST_CHECK(e.ok());
        BOOST_CHECK(e.type() == Error::NoError);
    }
    {
        Error e(Error::InvalidFormat);
        BOOST_CHECK(!e.ok());
        BOOST_CHECK(e.message() == "invalid format");
    }
    {
        Error e(Error::InvalidFormat, "custom");
        BOOST_CHECK(e.message() == "custom");
        BOOST_CHECK(e.type() == Error::InvalidFormat);
    }
    // A null message must not be handed to std::string.
    {
        Error e(Error::InvalidFormat, static_cast<const char *>(nullptr));
        BOOST_CHECK(e.message().empty());
    }
}

BOOST_AUTO_TEST_CASE(test_Error_Cause) {
    // A root error has no cause and renders as its own message.
    {
        Error root(Error::InvalidFormat, "unexpected token");
        BOOST_CHECK(root.cause().ok());
        BOOST_CHECK(root.toString() == "unexpected token");
        BOOST_CHECK(root.rootCause().message() == "unexpected token");
    }
    // One level of chaining.
    {
        Error root(Error::InvalidFormat, "unexpected token");
        Error wrapped = Error(Error::FileNotOpen, "cannot read desc.json").withCause(root);

        BOOST_CHECK(wrapped.type() == Error::FileNotOpen);
        BOOST_CHECK(wrapped.message() == "cannot read desc.json");
        BOOST_CHECK(wrapped.toString() == "cannot read desc.json: unexpected token");

        BOOST_CHECK(wrapped.cause().type() == Error::InvalidFormat);
        BOOST_CHECK(wrapped.rootCause().type() == Error::InvalidFormat);
    }
    // Several levels. The root type survives, which it does not when messages are spliced.
    {
        Error e = Error(Error::FileNotOpen, "outer")
                      .withCause(Error(Error::FileNotFound, "middle")
                                     .withCause(Error(Error::InvalidArgument, "inner")));

        BOOST_CHECK(e.toString() == "outer: middle: inner");
        BOOST_CHECK(e.rootCause().type() == Error::InvalidArgument);
        BOOST_CHECK(e.cause().cause().message() == "inner");
    }
    // A successful cause is dropped, so a call that may or may not have failed can be passed in.
    {
        Error e = Error(Error::FileNotOpen, "outer").withCause(Error::success());
        BOOST_CHECK(e.cause().ok());
        BOOST_CHECK(e.toString() == "outer");
    }
    // Levels without text of their own do not leave stray separators behind.
    {
        Error e = Error(Error::FileNotOpen, "").withCause(Error(Error::InvalidFormat, "inner"));
        BOOST_CHECK(e.toString() == "inner");
    }
    // \c withCause() returns a copy, leaving the original untouched.
    {
        Error original(Error::FileNotOpen, "outer");
        Error chained = original.withCause(Error(Error::InvalidFormat, "inner"));

        BOOST_CHECK(original.cause().ok());
        BOOST_CHECK(original.toString() == "outer");
        BOOST_CHECK(chained.toString() == "outer: inner");
    }
    // Copies share the chain rather than duplicating it.
    {
        Error e = Error(Error::FileNotOpen, "outer").withCause(Error(Error::InvalidFormat, "inner"));
        Error copy = e;
        BOOST_CHECK(copy.toString() == e.toString());
        BOOST_CHECK(copy.rootCause().type() == Error::InvalidFormat);
    }
}

BOOST_AUTO_TEST_SUITE_END()
