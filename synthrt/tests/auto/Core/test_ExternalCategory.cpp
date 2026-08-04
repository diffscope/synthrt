#include <filesystem>
#include <string>
#include <string_view>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Contribute.h>
#include <synthrt/Core/Contribute_p.h>
#include <synthrt/Core/SynthUnit.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::ContribSpec;
using srt::Expected;
using srt::JsonObject;
using srt::JsonValue;
using srt::SynthUnit;

// A contribute category defined outside synthrt, which is the whole point of this file.
//
// This test binary is a separate module that links synthrt, so it stands in for a library like
// wolf. Everything below is the pattern such a library follows, and it only compiles because the
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

BOOST_AUTO_TEST_SUITE(test_ExternalCategory)

// The registration reaches the same list synthrt's own categories are in.
//
// A library registers before main, which is early enough. A plugin would not be: it loads through
// a SynthUnit that has already built its categories, so its registration arrives too late to be
// of any use. That is a property of when the code runs, not of where it lives.
BOOST_AUTO_TEST_CASE(test_ExternalCategory_AppearsInRegistry) {
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
BOOST_AUTO_TEST_CASE(test_ExternalCategory_BuiltByEveryUnit) {
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
BOOST_AUTO_TEST_CASE(test_ExternalCategory_SpecCarriesItsIdentifier) {
    SampleSpec spec;

    BOOST_CHECK(spec.category() == "sample");
    BOOST_CHECK(spec.id().empty());
    BOOST_CHECK(spec.state() == ContribSpec::Invalid);

    JsonObject obj;
    BOOST_REQUIRE(spec.readFrom(obj));
    BOOST_CHECK(spec.id() == "greeting");
}

BOOST_AUTO_TEST_SUITE_END()
