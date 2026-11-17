#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

#include "SynthesisRunner.h"

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

BOOST_AUTO_TEST_CASE(failed_package_load_produces_no_output) {
    TemporaryDirectory temporary;
    srt::SynthUnit unit;
    ds::cli::SynthesisRunner runner(unit);
    ds::cli::SynthesisInput input;
    input.singer = "voice";
    const auto output = temporary.path / "output.wav";
    BOOST_CHECK_THROW(runner.run(temporary.path / "missing", std::move(input), output),
                      std::runtime_error);
    BOOST_CHECK(!std::filesystem::exists(output));
}
