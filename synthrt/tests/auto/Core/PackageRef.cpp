#include <synthrt/Core/PackageRef.h>

#include <boost/test/unit_test.hpp>

using namespace stdc;

BOOST_AUTO_TEST_SUITE(test_Contribute)

using Ver = stdc::VersionNumber;

BOOST_AUTO_TEST_CASE(test_PackageDependency) {
    // string
    {
        auto exp = srt::PackageDependency::fromJsonValue("foo[1.2.3]");
        BOOST_REQUIRE(exp.hasValue());
        BOOST_CHECK(exp.value() == srt::PackageDependency("foo", Ver(1, 2, 3)));
    }
    // json object with compat id
    {
        auto exp = srt::PackageDependency::fromJsonValue(srt::JsonObject({
            {"id", "foo[1.2.3]"},
        }));
        BOOST_REQUIRE(exp.hasValue());
        BOOST_CHECK(exp.value() == srt::PackageDependency("foo", Ver(1, 2, 3)));
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
    // json object with required
    {
        auto exp = srt::PackageDependency::fromJsonValue(srt::JsonObject({
            {"id",       "foo"  },
            {"version",  "1.2.3"},
            {"required", false  }
        }));
        BOOST_REQUIRE(exp.hasValue());
        BOOST_CHECK(exp.value() == srt::PackageDependency("foo", Ver(1, 2, 3), false));
    }
}

BOOST_AUTO_TEST_SUITE_END()