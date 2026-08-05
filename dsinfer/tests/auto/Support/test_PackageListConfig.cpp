#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <dsinfer/Support/PackageListConfig.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

using ds::PackageListConfig;
using ds::PackageListItem;
using ds::PackageListItemMetadata;
using Ver = stdc::VersionNumber;

namespace {

    /// A file under the system temporary directory, gone again when the test that made it ends.
    class TempFile {
    public:
        explicit TempFile(const char *name) : _path(fs::temp_directory_path() / name) {
            fs::remove(_path);
        }
        ~TempFile() {
            fs::remove(_path);
        }

        const fs::path &path() const {
            return _path;
        }

        void write(const std::string &content) const {
            std::ofstream file(_path);
            file << content;
        }

        std::string read() const {
            std::ifstream file(_path);
            return std::string(std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>());
        }

    private:
        fs::path _path;
    };

    PackageListConfig sample() {
        return PackageListConfig({
            PackageListItem("vendor/first", Ver(1, 0, 0, 0), "first-1.0.0.0",
                            PackageListItemMetadata(true, 1700000000)),
            PackageListItem("second", Ver(2, 1), "nested/second-2.1",
                            PackageListItemMetadata(false, 0)),
        });
    }

}

BOOST_AUTO_TEST_SUITE(test_PackageListConfig)

// What save writes, load has to read back. The two used to disagree about the shape of the
// document, and with nothing calling either of them nothing noticed.
BOOST_AUTO_TEST_CASE(test_PackageListConfig_RoundTrip) {
    TempFile file("dsinfer-test-package-list.json");

    auto saved = sample();
    BOOST_REQUIRE(saved.save(file.path()));
    BOOST_CHECK(!file.read().empty());

    PackageListConfig loaded;
    if (auto exp = loaded.load(file.path()); !exp) {
        BOOST_FAIL(exp.error().toString());
    }

    BOOST_REQUIRE(loaded.packages().size() == 2);

    const auto &first = loaded.packages()[0];
    BOOST_CHECK(first.id() == "vendor/first");
    BOOST_CHECK(first.version() == Ver(1, 0, 0, 0));
    BOOST_CHECK(first.relativeLocation() == fs::path("first-1.0.0.0"));
    BOOST_CHECK(first.metadata().hasSinger());
    BOOST_CHECK(first.metadata().installedTimestamp() == 1700000000);

    const auto &second = loaded.packages()[1];
    BOOST_CHECK(second.id() == "second");
    BOOST_CHECK(second.version() == Ver(2, 1));
    BOOST_CHECK(second.relativeLocation() == fs::path("nested/second-2.1"));
    BOOST_CHECK(!second.metadata().hasSinger());
    BOOST_CHECK(second.metadata().installedTimestamp() == 0);

    // Writing what was read produces the same file, so nothing is lost on the way through.
    TempFile again("dsinfer-test-package-list-2.json");
    BOOST_REQUIRE(loaded.save(again.path()));
    BOOST_CHECK(again.read() == file.read());
}

BOOST_AUTO_TEST_CASE(test_PackageListConfig_Empty) {
    TempFile file("dsinfer-test-package-list-empty.json");

    PackageListConfig empty;
    BOOST_REQUIRE(empty.save(file.path()));

    PackageListConfig loaded(sample().packages());
    BOOST_REQUIRE(loaded.load(file.path()));
    BOOST_CHECK(loaded.packages().empty());
}

BOOST_AUTO_TEST_CASE(test_PackageListConfig_MissingFile) {
    PackageListConfig config;
    auto exp = config.load(fs::temp_directory_path() / "dsinfer-test-does-not-exist.json");
    BOOST_CHECK(!exp);
    BOOST_CHECK(exp.error().code() == srt::Error::FileNotOpen);
}

BOOST_AUTO_TEST_CASE(test_PackageListConfig_Malformed) {
    TempFile file("dsinfer-test-package-list-bad.json");

    // Not JSON at all.
    file.write("{not json");
    {
        PackageListConfig config;
        BOOST_CHECK(!config.load(file.path()));
    }
    // JSON, but not the shape this reads.
    file.write("[]");
    {
        PackageListConfig config;
        BOOST_CHECK(!config.load(file.path()));
    }
    // The right shape with the array missing.
    file.write("{}");
    {
        PackageListConfig config;
        BOOST_CHECK(!config.load(file.path()));
    }
}

// An entry that does not say what it is gets left out, rather than becoming a package with an
// empty identifier that later lookups would have to keep stepping over.
BOOST_AUTO_TEST_CASE(test_PackageListConfig_SkipsIncompleteEntries) {
    TempFile file("dsinfer-test-package-list-partial.json");
    file.write(R"({"packages":[
        {"id":"good=1.0","relativeLocation":"good","metadata":{"hasSinger":true}},
        {"relativeLocation":"no-id","metadata":{}},
        {"id":"no-version","relativeLocation":"x","metadata":{}},
        {"id":"has/contribute=1.0:singer/main","relativeLocation":"x","metadata":{}},
        {"id":"no-location=1.0","metadata":{}},
        {"id":"no-metadata=1.0","relativeLocation":"x"},
        "not even an object"
    ]})");

    PackageListConfig config;
    BOOST_REQUIRE(config.load(file.path()));
    BOOST_REQUIRE(config.packages().size() == 1);
    BOOST_CHECK(config.packages()[0].id() == "good");
    BOOST_CHECK(config.packages()[0].version() == Ver(1, 0));
}

BOOST_AUTO_TEST_SUITE_END()
