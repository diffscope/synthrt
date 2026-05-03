#include <synthrt/Core/PackageRef.h>

#include <boost/test/unit_test.hpp>

using namespace stdc;

BOOST_AUTO_TEST_SUITE(test_Contribute)

using Ver = stdc::VersionNumber;

BOOST_AUTO_TEST_CASE(test_PackageDependency) {
    // string is no longer supported
    {
        auto exp = srt::PackageDependency::fromJsonValue("foo[1.2.3]");
        BOOST_CHECK(!exp.hasValue());
    }
    // json object requires an explicit version
    {
        auto exp = srt::PackageDependency::fromJsonValue(srt::JsonObject({
            {"id", "foo"},
        }));
        BOOST_CHECK(!exp.hasValue());
    }
    // json object
    {
        auto exp = srt::PackageDependency::fromJsonValue(srt::JsonObject({
            {"id",      "foo"  },
            {"version", "1.2.3"}
        }));
        BOOST_REQUIRE(exp.hasValue());
        BOOST_CHECK(exp.value() == srt::PackageDependency("foo", Ver(1, 2, 3)));
    }
    // package identifiers may contain slash segments
    {
        auto exp = srt::PackageDependency::fromJsonValue(srt::JsonObject({
            {"id",      "foo/bar"},
            {"version", "1.2.3"  }
        }));
        BOOST_REQUIRE(exp.hasValue());
        BOOST_CHECK(exp.value() == srt::PackageDependency("foo/bar", Ver(1, 2, 3)));
    }
}

BOOST_AUTO_TEST_SUITE_END()
