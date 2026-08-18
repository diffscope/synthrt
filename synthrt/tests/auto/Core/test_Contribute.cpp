#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Core/ContribHandler.h>
#include <synthrt/Core/Contribute_p.h>
#include <synthrt/Core/PackageRef.h>
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
        class Handler : public srt::ContribSpecHandler {
        public:
            Expected<void> read(const std::filesystem::path &basePath, const JsonObject &obj) {
                (void) basePath;
                (void) obj;

                // No identifier here. What this module is called is the package's business, and
                // PackageRef fills it in from desc.json once this returns.
                fmtVersion = stdc::VersionNumber(1, 0);
                return Expected<void>();
            }

            // Where the package pointed. A real category would open it; this one only records it,
            // so a test can check what the framework handed over.
            std::string pointedAt;
        };

        SampleSpec() : ContribSpec("sample", std::make_unique<Handler>()) {
        }

        const std::string &pointedAt() const {
            srt_handler_t;
            return handler.pointedAt;
        }

        void setPointedAt(std::string path) {
            srt_handler_t;
            handler.pointedAt = std::move(path);
        }

        Expected<void> readFrom(const JsonObject &obj) {
            srt_handler_t;
            return handler.read({}, obj);
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
            auto spec = new SampleSpec();
            spec->setPointedAt(config.toString());
            return spec;
        }

        explicit SampleCategory(SynthUnit *su) : srt::ContribCategory(*new Impl(this, su)) {
        }

        friend class srt::ContribCategoryFactory<SampleCategory>;
    };

}

static srt::ContribCategoryRegistry::Add<srt::ContribCategoryFactory<SampleCategory>>
    registrar("sample", "Sample contributes, defined outside synthrt");

namespace {

    namespace fs = std::filesystem;

    /// A package directory holding nothing but a desc.json, removed when the test leaves scope.
    ///
    /// The modules it names are never written. A package is parsed by reading desc.json and
    /// handing each path to its category, and SampleCategory does not open what it is given -- so
    /// these cases reach the framework's own handling and stop there.
    class TempPackage {
    public:
        explicit TempPackage(const std::string &desc) {
            static int counter = 0;
            _dir = fs::temp_directory_path() / ("srt_test_package_" + std::to_string(++counter));

            std::error_code ec;
            fs::remove_all(_dir, ec);
            fs::create_directories(_dir);

            std::ofstream file(_dir / "desc.json", std::ios::binary);
            file << desc;
        }

        ~TempPackage() {
            std::error_code ec;
            fs::remove_all(_dir, ec);
        }

        const fs::path &dir() const {
            return _dir;
        }

        TempPackage(const TempPackage &) = delete;
        TempPackage &operator=(const TempPackage &) = delete;

    private:
        fs::path _dir;
    };

}

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

// A spec starts out belonging to nobody. Its identifier arrives from the package that names it,
// which is what the cases below go through desc.json to check.
BOOST_AUTO_TEST_CASE(test_ContribSpec_StartsWithoutAnIdentifier) {
    SampleSpec spec;

    BOOST_CHECK(spec.category() == "sample");
    BOOST_CHECK(spec.id().empty());
    BOOST_CHECK(spec.state() == ContribSpec::Invalid);

    JsonObject obj;
    BOOST_REQUIRE(spec.readFrom(obj));
    BOOST_CHECK(spec.id().empty());
}

// desc.json names each contribute and says where it is. The name is the package's -- it is what a
// reference from elsewhere resolves against -- so it lives here rather than in the module's own
// manifest, and the module is never asked what it is called.
BOOST_AUTO_TEST_CASE(test_PackageRef_TakesContributeIdsFromDesc) {
    // "dependencies" is written out because the loader requires it, even though the format
    // document calls it optional.
    TempPackage pkg(R"({
        "id": "vendor/sample",
        "version": "1.0.0.0",
        "dependencies": [],
        "contributes": {
            "sample": [
                { "id": "greeting", "path": "./greeting.json" },
                { "id": "farewell", "path": "./elsewhere/farewell.json" }
            ]
        }
    })");

    SynthUnit su;
    auto opened = su.open(pkg.dir(), true);
    if (!opened) {
        BOOST_TEST_MESSAGE(opened.error().message());
    }
    BOOST_REQUIRE(opened);

    // Closed on the way out. A unit releases what it loaded when it goes, but not what was only
    // opened, so a reference taken in this mode is the caller's to give back.
    srt::ScopedPackageRef ref(opened.take());

    auto specs = ref.contributes("sample");
    BOOST_REQUIRE(specs.size() == 2);

    // Sorted by identifier, which is how the package indexes them.
    BOOST_CHECK(specs[0]->id() == "farewell");
    BOOST_CHECK(specs[1]->id() == "greeting");
    BOOST_CHECK(specs[0]->category() == "sample");

    // The path reached the category untouched, for it to resolve as it sees fit.
    BOOST_CHECK(specs[0]->as<SampleSpec>()->pointedAt() == "./elsewhere/farewell.json");
    BOOST_CHECK(specs[1]->as<SampleSpec>()->pointedAt() == "./greeting.json");

    // And one can be found by the name the package gave it.
    BOOST_CHECK(ref.contribute("sample", "greeting") == specs[1]);
    BOOST_CHECK(ref.contribute("sample", "nothing") == nullptr);
}

// Whatever is wrong with an entry is wrong before any module is opened.
BOOST_AUTO_TEST_CASE(test_PackageRef_RejectsBadContributeEntries) {
    auto rejected = [](const std::string &entries) {
        TempPackage pkg(
            R"({"id": "vendor/sample", "version": "1.0.0.0", "dependencies": [], "contributes": )" +
            entries + "}");
        SynthUnit su;
        auto opened = su.open(pkg.dir(), true);
        if (!opened) {
            return true;
        }
        // A package that opened but did not parse comes back carrying the reason.
        srt::ScopedPackageRef ref(opened.take());
        return !ref.error().ok();
    };

    BOOST_CHECK(rejected(R"({"sample": ["./greeting.json"]})"));               // no longer a path
    BOOST_CHECK(rejected(R"({"sample": [{"path": "./greeting.json"}]})"));     // no id
    BOOST_CHECK(rejected(R"({"sample": [{"id": "greeting"}]})"));              // no path
    BOOST_CHECK(rejected(R"({"sample": [{"id": "", "path": "./g.json"}]})"));  // not a segment
    BOOST_CHECK(rejected(R"({"sample": [{"id": "a/b", "path": "./g.json"}]})")); // nor is this
    BOOST_CHECK(rejected(R"({"sample": [{"id": "g", "path": 1}]})"));          // path is not text
    BOOST_CHECK(rejected(R"({"sample": [{"id": "g", "path": "./g.json", "extra": 1}]})"));
    BOOST_CHECK(rejected(R"({"unregistered": [{"id": "g", "path": "./g.json"}]})"));

    // Two entries claiming one name, which is caught without opening either.
    BOOST_CHECK(rejected(
        R"({"sample": [{"id": "g", "path": "./a.json"}, {"id": "g", "path": "./b.json"}]})"));

    // The shape that works, so the cases above fail for the reason intended and not because the
    // surrounding document is wrong.
    BOOST_CHECK(!rejected(R"({"sample": [{"id": "g", "path": "./g.json"}]})"));
}

BOOST_AUTO_TEST_SUITE_END()
