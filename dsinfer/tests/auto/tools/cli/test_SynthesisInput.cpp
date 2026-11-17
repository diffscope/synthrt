#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "SynthesisInput.h"

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

BOOST_AUTO_TEST_CASE(load_valid_input_and_reject_malformed_envelopes) {
    TemporaryDirectory temporary;
    auto valid = ds::cli::SynthesisInput::load(
        temporary.write("valid.json", R"({"singer":"voice","duration":1.25,"words":[]})"));
    BOOST_REQUIRE(valid);
    BOOST_CHECK_EQUAL(valid.value().singer, "voice");
    BOOST_REQUIRE(valid.value().acoustic);
    BOOST_CHECK_EQUAL(valid.value().acoustic->duration, 1.25);
    int index = 0;
    for (const auto &text : {"{", "[]", "{}", R"({"singer":1})", R"({"singer":""})",
                             R"({"singer":"voice","duration":"bad"})"}) {
        auto invalid =
            ds::cli::SynthesisInput::load(temporary.write(std::to_string(index++), text));
        BOOST_REQUIRE(!invalid);
        BOOST_CHECK(invalid.error().code() == srt::Error::InvalidFormat);
    }
    auto missing = ds::cli::SynthesisInput::load(temporary.path / "absent");
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotOpen);
}
