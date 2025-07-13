#include <synthrt/Core/Contribute.h>

#include <boost/test/unit_test.hpp>

using namespace stdc;

BOOST_AUTO_TEST_SUITE(test_Contribute)

using Ver = stdc::VersionNumber;

BOOST_AUTO_TEST_CASE(test_ContribLocator) {
    // id
    {
        auto loc = srt::ContribLocator::fromString("id");
        BOOST_CHECK(loc == srt::ContribLocator("id"));
    }
    {
        auto loc = srt::ContribLocator::fromString("a-b");
        BOOST_CHECK(loc == srt::ContribLocator("a-b"));
    }
    // package/id
    {
        auto loc = srt::ContribLocator::fromString("package/id");
        BOOST_CHECK(loc == srt::ContribLocator("package", {}, "id"));
    }
    {
        auto loc = srt::ContribLocator::fromString("a-b/c-d");
        BOOST_CHECK(loc == srt::ContribLocator("a-b", {}, "c-d"));
    }
    // package[version]/id
    {
        auto loc = srt::ContribLocator::fromString("package[1.2.3]/id");
        BOOST_CHECK(loc == srt::ContribLocator("package", Ver(1, 2, 3), "id"));
    }
    {
        auto loc = srt::ContribLocator::fromString("a-b[1.2.3]/c-d");
        BOOST_CHECK(loc == srt::ContribLocator("a-b", Ver(1, 2, 3), "c-d"));
    }

    // errors
    {
        auto loc = srt::ContribLocator::fromString("");
        BOOST_CHECK(loc == srt::ContribLocator());
    }
    {
        auto loc = srt::ContribLocator::fromString("a/b/c");
        BOOST_CHECK(loc == srt::ContribLocator());
    }
    {
        auto loc = srt::ContribLocator::fromString("a[1.2.3]");
        BOOST_CHECK(loc == srt::ContribLocator());
    }
    {
        auto loc = srt::ContribLocator::fromString("a][1.2]/b");
        BOOST_CHECK(loc == srt::ContribLocator());
    }
    {
        auto loc = srt::ContribLocator::fromString("a:/b");
        BOOST_CHECK(loc == srt::ContribLocator());
    }
    {
        auto loc = srt::ContribLocator::fromString("a[1.2]xy/b");
        BOOST_CHECK(loc == srt::ContribLocator());
    }
}

BOOST_AUTO_TEST_SUITE_END()