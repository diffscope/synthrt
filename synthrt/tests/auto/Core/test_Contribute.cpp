#include <filesystem>
#include <string>
#include <string_view>

#include <synthrt/Core/Contribute.h>
#include <synthrt/Core/Contribute_p.h>
#include <synthrt/Core/SynthUnit.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using namespace stdc;

using srt::ContribSpec;
using srt::Expected;
using srt::JsonObject;
using srt::JsonValue;
using srt::SynthUnit;

// A contribute category defined outside synthrt.
//
// This test binary is a separate module that links synthrt, so it stands in for a library like
// wolf. Everything here is the pattern such a library follows, and it only compiles because the
// implementation classes are reachable through Contribute_p.h and the registry is exported.
namespace {

    class SampleCategory;

    class SampleSpec : public ContribSpec {
    public:
        class Impl : public ContribSpec::Impl {
        public:
            Impl() : ContribSpec::Impl("sample") {
            }

            Expected<void> read(const std::filesystem::path &basePath,
                                const JsonObject &obj) override {
                (void) basePath;
                (void) obj;

                // Setting the identifier is the reason the implementation class has to be
                // derivable at all: PackageRef reads it straight back off the parsed spec, and
                // nothing public can write it.
                id = "greeting";
                fmtVersion = stdc::VersionNumber(1, 0);
                return Expected<void>();
            }
        };

        SampleSpec() : ContribSpec(*new Impl()) {
        }

        Expected<void> readFrom(const JsonObject &obj) {
            return static_cast<Impl *>(_impl.get())->read({}, obj);
        }
    };

    class SampleCategory : public srt::ContribCategory {
    public:
        class Impl : public srt::ContribCategory::Impl {
        public:
            Impl(SampleCategory *decl, SynthUnit *su)
                : srt::ContribCategory::Impl(decl, "sample", su) {
            }
        };

    protected:
        Expected<ContribSpec *> parseSpec(const std::filesystem::path &basePath,
                                          const JsonValue &config) const override {
            (void) basePath;
            (void) config;
            return new SampleSpec();
        }

        explicit SampleCategory(SynthUnit *su) : srt::ContribCategory(*new Impl(this, su)) {
        }

        friend class srt::ContribCategoryFactory<SampleCategory>;
    };

}

static srt::ContribCategoryRegistry::Add<srt::ContribCategoryFactory<SampleCategory>>
    registrar("sample", "Sample contributes, defined outside synthrt");

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
        "",                                // names nothing
        ":",                               // empty contribute part
        "=1.0.0.0:singer/main",            // a version with no package to apply it to
        "vendor/sample:singer/main/extra", // more than one "/" after the ":"
        "vendor/sample:a:b",               // more than one ":"
        "vendor/sample=1.0=2.0",           // more than one "="
        "vendor/sample=1.2.3.4.5",         // more than four version segments
        "vendor/sample=01.0",              // leading zero in a version segment
        "vendor/sample=",                  // empty version
        "vendor/sample@1.0:singer/main",   // "@" is not in the character set
        "vendor/ sample:singer/main",      // nor is a space
        "vendor/sample:sing er/main",      //
        "vendor//sample",                  // empty package segment
        "vendor/sample:/main",             // empty category
        "vendor/sample:singer/",           // empty identifier
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
        BOOST_CHECK_MESSAGE(parsed.toString() == token, std::string("round trip changed ") + token +
                                                            " into " + parsed.toString());
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

// The registration reaches the same list synthrt's own categories are in.
//
// A library registers before main, which is early enough. A plugin would not be: it loads through
// a SynthUnit that has already built its categories, so its registration arrives too late to be
// of any use. That is a property of when the code runs, not of where it lives.
BOOST_AUTO_TEST_CASE(test_ContribCategoryRegistry_TakesOneFromOutside) {
    bool found = false;
    int count = 0;
    for (const auto &entry : srt::ContribCategoryRegistry::entries()) {
        ++count;
        if (entry.name() == "sample") {
            found = true;
            BOOST_CHECK(!entry.desc().empty());
        }
    }
    BOOST_CHECK(found);

    // synthrt's own two are still there, unaffected.
    BOOST_CHECK(count == 3);
}

// Every unit builds the outside category alongside the built-in ones.
BOOST_AUTO_TEST_CASE(test_ContribCategory_BuiltByEveryUnit) {
    SynthUnit su;

    auto sample = su.category("sample");
    BOOST_REQUIRE(sample != nullptr);
    BOOST_CHECK(sample->name() == "sample");
    BOOST_CHECK(sample->SU() == &su);
    BOOST_CHECK(sample->as<SampleCategory>() != nullptr);

    BOOST_CHECK(su.category("singer") != nullptr);
    BOOST_CHECK(su.category("inference") != nullptr);

    SynthUnit other;
    BOOST_CHECK(other.category("sample") != nullptr);
    BOOST_CHECK(other.category("sample") != sample);
}

// A spec defined outside synthrt can carry the identifier PackageRef will ask it for.
BOOST_AUTO_TEST_CASE(test_ContribSpec_CarriesItsIdentifier) {
    SampleSpec spec;

    BOOST_CHECK(spec.category() == "sample");
    BOOST_CHECK(spec.id().empty());
    BOOST_CHECK(spec.state() == ContribSpec::Invalid);

    JsonObject obj;
    BOOST_REQUIRE(spec.readFrom(obj));
    BOOST_CHECK(spec.id() == "greeting");
}

BOOST_AUTO_TEST_SUITE_END()
