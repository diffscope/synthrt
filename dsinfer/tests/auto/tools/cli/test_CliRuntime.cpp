#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>

#include "CliRuntime.h"

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

BOOST_AUTO_TEST_CASE(missing_driver_is_a_runtime_error) {
    TemporaryDirectory temporary;
    BOOST_CHECK_THROW(
        ds::cli::CliRuntime(temporary.path, ds::Api::Onnx::ExecutionProvider::CPU, -1),
        std::runtime_error);
}
