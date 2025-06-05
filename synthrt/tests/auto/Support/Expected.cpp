#include <synthrt/Support/Expected.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_Expected)

using srt::Error;
using srt::Expected;

BOOST_AUTO_TEST_CASE(test_Expected) {
    // Construct from error
    {
        Expected<std::string> e((Error(Error::InvalidArgument)));
        BOOST_VERIFY(!e.hasValue());
        BOOST_VERIFY(e.error().type() == Error::InvalidArgument);
        BOOST_VERIFY(e.error().message() == Error(Error::InvalidArgument).message());
    }
    // Construct from value
    {
        Expected<std::string> e("hello");
        BOOST_VERIFY(e.hasValue());
        BOOST_VERIFY(e.get() == "hello");
    }
    // Construct from convertible Expected
    {
        Expected<std::string> e1 = Expected<const char *>(Error(Error::InvalidArgument));
        BOOST_VERIFY(!e1.hasValue());
        BOOST_VERIFY(e1.error().type() == Error::InvalidArgument);

        Expected<std::string> e2 = Expected<const char *>("hello");
        BOOST_VERIFY(e2.hasValue());
        BOOST_VERIFY(e2.get() == "hello");
    }
}

BOOST_AUTO_TEST_SUITE_END()