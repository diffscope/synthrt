#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <inferutil/SpeakerEmbedding.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    struct TemporaryDirectory {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("support-tests-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryDirectory() {
            std::filesystem::create_directories(path);
        }
        ~TemporaryDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
        std::filesystem::path write(const std::string &name, const std::string &text) {
            const auto result = path / name;
            std::ofstream stream(result, std::ios::binary);
            stream << text;
            BOOST_REQUIRE(stream.good());
            return result;
        }
    };

}

BOOST_AUTO_TEST_CASE(embedding_files_report_short_reads_and_invalid_dimensions) {
    TemporaryDirectory temporary;
    auto invalid = ds::inferutil::loadSpeakerEmbedding(0, temporary.path / "missing");
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidArgument);
    auto missing = ds::inferutil::loadSpeakerEmbedding(2, temporary.path / "missing");
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotOpen);
    const auto shortPath = temporary.write("short", "x");
    BOOST_CHECK(!ds::inferutil::loadSpeakerEmbedding(2, shortPath));
    const std::vector<float> expected{0.5f, -1.5f};
    const auto path = temporary.path / "embedding";
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(expected.data()),
                     expected.size() * sizeof(float));
        BOOST_REQUIRE(stream.good());
    }
    auto loaded = ds::inferutil::loadSpeakerEmbedding(2, path);
    BOOST_REQUIRE(loaded);
    BOOST_CHECK(loaded.value() == expected);
}

BOOST_AUTO_TEST_CASE(speaker_mixtures_are_weighted_and_shape_checked) {
    using ds::inferutil::preprocessSpeakerEmbeddingFrames;
    const ds::inferutil::SpeakerMapping mapping{
        {"a", "left" },
        {"b", "right"}
    };
    auto mixed = preprocessSpeakerEmbeddingFrames(
        {
            {"a", 0.01, {0.25}},
            {"b", 0.01, {0.75}}
    },
        mapping, {{"left", {4, 0}}, {"right", {0, 8}}}, 2, 0.01, 2);
    BOOST_REQUIRE(mixed);
    const auto data = mixed.value()->data<float>();
    BOOST_REQUIRE(data);
    BOOST_CHECK_EQUAL(data[0], 1);
    BOOST_CHECK_EQUAL(data[1], 6);
    BOOST_CHECK_EQUAL(data[2], 1);
    BOOST_CHECK_EQUAL(data[3], 6);
    auto unknown = preprocessSpeakerEmbeddingFrames(
        {
            {"missing", 0.01, {1}}
    },
        {}, {}, 2, 0.01, 1);
    BOOST_REQUIRE(!unknown);
    BOOST_CHECK(unknown.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(!preprocessSpeakerEmbeddingFrames(
        {
            {"a", 0.01, {1}}
    },
        {}, {{"a", {1}}}, 2, 0.01, 1));
}
