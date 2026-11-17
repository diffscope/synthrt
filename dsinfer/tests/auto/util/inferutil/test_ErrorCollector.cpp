#include <string>
#include <utility>

#include <inferutil/ErrorCollector.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(collect_format_clear_and_reuse) {
    ds::inferutil::ErrorCollector errors;
    BOOST_CHECK(!errors.hasErrors());
    BOOST_CHECK(errors.getErrorMessage("invalid").empty());
    errors.collectError("first");
    BOOST_CHECK_EQUAL(errors.getErrorMessage("invalid"), "invalid (1 error found):\n1. first");
    const std::string second = "second";
    errors.collectError(second);
    std::string third = "third";
    errors.collectError(std::move(third));
    BOOST_REQUIRE_EQUAL(errors.errors().size(), 3u);
    BOOST_CHECK_EQUAL(errors.errors()[1], "second");
    BOOST_CHECK_EQUAL(errors.errors()[2], "third");
    BOOST_CHECK_EQUAL(errors.getErrorMessage("invalid"),
                      "invalid (3 errors found):\n1. first;\n2. second;\n3. third");
    errors.clear();
    BOOST_CHECK(!errors.hasErrors());
    BOOST_CHECK(errors.errors().empty());
    errors.collectError("again");
    BOOST_CHECK_EQUAL(errors.errors().front(), "again");
}
