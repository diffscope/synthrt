#include <string>
#include <string_view>

#include <synthrt/Core/ContribInterpreterPlugin.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    stdc::json::Value parse(std::string_view text) {
        stdc::json::ParseError error;
        auto value = stdc::json::Value::fromJson(text, false, &error);
        BOOST_REQUIRE_MESSAGE(!error, "the test metadata is not JSON: " + std::string(text));
        return value;
    }

    // The reasons a metadata document is refused all share one code, so a case checks the code
    // and that some reason was given.
    void checkRefused(std::string_view text) {
        const auto result = srt::ContribInterpreterPlugin::readInterpreterDeclarations(parse(text));
        BOOST_REQUIRE_MESSAGE(!result, "accepted: " + std::string(text));
        BOOST_CHECK(result.error().code() == srt::Error::InvalidFormat);
        BOOST_CHECK(!result.error().message().empty());
    }

}

BOOST_AUTO_TEST_SUITE(test_ContribInterpreterPlugin)

BOOST_AUTO_TEST_CASE(test_declarations_are_read_in_order) {
    const auto result = srt::ContribInterpreterPlugin::readInterpreterDeclarations(parse(R"({
        "name": "example",
        "interpreters": [
            {"interface": "com.example.Second", "level": 3, "variant": "native"},
            {"interface": "com.example.First", "level": 1, "variant": "com.example.remote"}
        ]
    })"));
    BOOST_REQUIRE_MESSAGE(result, (result ? std::string() : result.error().message()));
    const auto &declarations = result.value();
    BOOST_REQUIRE_EQUAL(declarations.size(), 2u);
    BOOST_CHECK_EQUAL(declarations[0].interfaceId(), "com.example.Second");
    BOOST_CHECK_EQUAL(declarations[0].level(), 3);
    BOOST_CHECK_EQUAL(declarations[0].variant(), "native");
    BOOST_CHECK_EQUAL(declarations[1].interfaceId(), "com.example.First");
    BOOST_CHECK_EQUAL(declarations[1].level(), 1);
    BOOST_CHECK_EQUAL(declarations[1].variant(), "com.example.remote");
}

BOOST_AUTO_TEST_CASE(test_the_bundle_needs_a_name_and_interpreters) {
    checkRefused(R"([])");
    checkRefused(R"({"interpreters": [{"interface": "a.B", "level": 1, "variant": "v"}]})");
    checkRefused(
        R"({"name": "a/b", "interpreters": [{"interface": "a.B", "level": 1, "variant": "v"}]})");
    checkRefused(R"({"name": "example"})");
    checkRefused(R"({"name": "example", "interpreters": {}})");
    checkRefused(R"({"name": "example", "interpreters": []})");
}

BOOST_AUTO_TEST_CASE(test_each_declaration_is_a_valid_triple) {
    checkRefused(R"({"name": "example", "interpreters": ["a.B"]})");
    checkRefused(R"({"name": "example", "interpreters": [{"level": 1, "variant": "v"}]})");
    checkRefused(
        R"({"name": "example", "interpreters": [{"interface": "a b", "level": 1, "variant": "v"}]})");
    checkRefused(R"({"name": "example", "interpreters": [{"interface": "a.B", "level": 1}]})");
    checkRefused(
        R"({"name": "example", "interpreters": [{"interface": "a.B", "level": 1, "variant": ""}]})");
    checkRefused(
        R"({"name": "example", "interpreters": [{"interface": "a.B", "level": "1", "variant": "v"}]})");
    checkRefused(
        R"({"name": "example", "interpreters": [{"interface": "a.B", "level": 0, "variant": "v"}]})");
    checkRefused(
        R"({"name": "example", "interpreters": [{"interface": "a.B", "level": -1, "variant": "v"}]})");
    checkRefused(
        R"({"name": "example", "interpreters": [{"interface": "a.B", "level": 4294967296, "variant": "v"}]})");
}

// A repeated triple refuses the whole bundle, because discovery could not tell which of the two
// entries the plugin meant.
BOOST_AUTO_TEST_CASE(test_a_repeated_declaration_refuses_the_bundle) {
    checkRefused(R"({"name": "example", "interpreters": [
        {"interface": "a.B", "level": 1, "variant": "v"},
        {"interface": "a.B", "level": 1, "variant": "v"}
    ]})");

    // The same interface at two levels is two declarations, not a repetition.
    const auto twoLevels = srt::ContribInterpreterPlugin::readInterpreterDeclarations(
        parse(R"({"name": "example", "interpreters": [
        {"interface": "a.B", "level": 1, "variant": "v"},
        {"interface": "a.B", "level": 2, "variant": "v"}
    ]})"));
    BOOST_REQUIRE(twoLevels);
    BOOST_CHECK_EQUAL(twoLevels.value().size(), 2u);
}

BOOST_AUTO_TEST_SUITE_END()
