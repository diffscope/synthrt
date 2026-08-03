#include <synthrt/Core/Contribute.h>

#include <boost/test/unit_test.hpp>

using namespace stdc;

BOOST_AUTO_TEST_SUITE(test_Contribute)

using Ver = stdc::VersionNumber;
using srt::ContribLocator;

static ContribLocator loc(std::string package, Ver version, std::string category, std::string id) {
    return ContribLocator(std::move(package), version, std::move(category), std::move(id));
}

BOOST_AUTO_TEST_CASE(test_ContribLocator_Parse) {
    // Fully qualified.
    BOOST_CHECK(ContribLocator::fromString("vendor/sample=1.0.0.0:inference/acoustic") ==
                loc("vendor/sample", Ver(1, 0, 0, 0), "inference", "acoustic"));
    BOOST_CHECK(ContribLocator::fromString("a-b=1.2.3:singer/c-d") ==
                loc("a-b", Ver(1, 2, 3), "singer", "c-d"));

    // Category left to resolution.
    BOOST_CHECK(ContribLocator::fromString("vendor/sample=1.0.0.0:acoustic") ==
                loc("vendor/sample", Ver(1, 0, 0, 0), "", "acoustic"));

    // Version left to resolution.
    BOOST_CHECK(ContribLocator::fromString("vendor/sample:singer/main") ==
                loc("vendor/sample", Ver(), "singer", "main"));

    // The current package.
    BOOST_CHECK(ContribLocator::fromString(":singer/main") == loc("", Ver(), "singer", "main"));
    BOOST_CHECK(ContribLocator::fromString(":main") == loc("", Ver(), "", "main"));

    // The package itself.
    BOOST_CHECK(ContribLocator::fromString("vendor/sample=1.0.0.0") ==
                loc("vendor/sample", Ver(1, 0, 0, 0), "", ""));
    BOOST_CHECK(ContribLocator::fromString("vendor/sample") == loc("vendor/sample", Ver(), "", ""));
    BOOST_CHECK(ContribLocator::fromString("sample=1") == loc("sample", Ver(1), "", ""));

    // Without a package part the leading ":" is what marks a contribute reference. Left out, this
    // names the package "singer/main" rather than the contribute "main" of category "singer".
    BOOST_CHECK(ContribLocator::fromString("singer/main") == loc("singer/main", Ver(), "", ""));
}

BOOST_AUTO_TEST_CASE(test_ContribLocator_Reject) {
    const char *invalid[] = {
        "",                                  // names nothing
        ":",                                 // empty contribute part
        "=1.0.0.0:singer/main",              // a version with no package to apply it to
        "vendor/sample:singer/main/extra",   // more than one "/" after the ":"
        "vendor/sample:a:b",                 // more than one ":"
        "vendor/sample=1.0=2.0",             // more than one "="
        "vendor/sample=1.2.3.4.5",           // more than four version segments
        "vendor/sample=01.0",                // leading zero in a version segment
        "vendor/sample=",                    // empty version
        "vendor/sample@1.0:singer/main",     // "@" is not in the character set
        "vendor/ sample:singer/main",        // nor is a space
        "vendor/sample:sing er/main",        //
        "vendor//sample",                    // empty package segment
        "vendor/sample:/main",               // empty category
        "vendor/sample:singer/",             // empty identifier
    };
    for (const auto *token : invalid) {
        BOOST_CHECK_MESSAGE(ContribLocator::fromString(token).isEmpty(),
                            std::string("should have been rejected: ") + token);
    }
}

BOOST_AUTO_TEST_CASE(test_ContribLocator_RoundTrip) {
    const char *references[] = {
        "vendor/sample=1.0:inference/acoustic",
        "vendor/sample=1.0:singer/main",
        "vendor/sample=1.0:acoustic",
        "vendor/sample:singer/main",
        ":singer/main",
        ":main",
        "vendor/sample=1.0",
        "vendor/sample",
        "a/b/c=1.2:singer/x",
    };
    for (const auto *token : references) {
        const auto parsed = ContribLocator::fromString(token);
        BOOST_CHECK_MESSAGE(parsed.toString() == token,
                            std::string("round trip changed ") + token + " into " +
                                parsed.toString());
        BOOST_CHECK(ContribLocator::fromString(parsed.toString()) == parsed);
    }
}

// A version goes through stdc::VersionNumber, which drops trailing zero segments, so rendering is
// not always byte for byte what was parsed. What has to hold is that the value survives.
BOOST_AUTO_TEST_CASE(test_ContribLocator_VersionIsNormalized) {
    const auto parsed = ContribLocator::fromString("vendor/sample=1.0.0.0:singer/main");
    BOOST_CHECK(parsed.version() == Ver(1, 0, 0, 0));
    BOOST_CHECK(parsed.toString() == "vendor/sample=1.0:singer/main");
    BOOST_CHECK(ContribLocator::fromString(parsed.toString()) == parsed);
}

BOOST_AUTO_TEST_CASE(test_ContribLocator_Segments) {
    BOOST_CHECK(ContribLocator::isValidSegment("a"));
    BOOST_CHECK(ContribLocator::isValidSegment("A-b_9"));
    BOOST_CHECK(!ContribLocator::isValidSegment(""));
    BOOST_CHECK(!ContribLocator::isValidSegment("a/b"));
    BOOST_CHECK(!ContribLocator::isValidSegment("a b"));
    BOOST_CHECK(!ContribLocator::isValidSegment("a.b"));

    BOOST_CHECK(ContribLocator::isValidPackageId("a"));
    BOOST_CHECK(ContribLocator::isValidPackageId("vendor/sample"));
    BOOST_CHECK(ContribLocator::isValidPackageId("a/b/c"));
    BOOST_CHECK(!ContribLocator::isValidPackageId(""));
    BOOST_CHECK(!ContribLocator::isValidPackageId("/a"));
    BOOST_CHECK(!ContribLocator::isValidPackageId("a/"));
    BOOST_CHECK(!ContribLocator::isValidPackageId("a//b"));
}

BOOST_AUTO_TEST_SUITE_END()
