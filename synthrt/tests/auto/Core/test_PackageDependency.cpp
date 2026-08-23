#include <synthrt/Core/PackageDependency.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_PackageDependency)

BOOST_AUTO_TEST_CASE(test_reads_dependency) {
    const auto value =
        srt::JsonValue::fromJson(R"({"id":"vendor/sample","version":"1.2.3"})", false);
    auto result = srt::PackageDependency::fromJsonValue(value);
    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(result->id, "vendor/sample");
    BOOST_CHECK(result->version == stdc::VersionNumber(1, 2, 3));
}

BOOST_AUTO_TEST_CASE(test_rejects_optional_and_duplicate_era_fields) {
    for (const auto *json : {
             R"({"id":"sample","version":"1","required":true})",
             R"({"id":"sample","version":"01"})",
             R"({"id":"sample"})",
             R"({"version":"1"})",
         }) {
        auto result = srt::PackageDependency::fromJsonValue(srt::JsonValue::fromJson(json, false));
        BOOST_CHECK_MESSAGE(!result, json);
    }
}

BOOST_AUTO_TEST_SUITE_END()
