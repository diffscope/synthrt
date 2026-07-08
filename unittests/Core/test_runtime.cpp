#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>

namespace {

    std::filesystem::path makeTempRoot(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-core-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

}

TEST_CASE("Runtime rejects package scans after initialize", "[runtime][packages]") {
    const auto root = makeTempRoot("immutable-package-sources");
    std::filesystem::create_directories(root / "demo-package");

    srt::core::Runtime runtime;
    REQUIRE(runtime.initialize().hasValue());

    auto result = runtime.scanPackages(root);
    CHECK(!result.hasValue());
    CHECK(result.error().type() == srt::core::Error::InvalidArgument);
    CHECK(result.error().code() == srt::core::ErrorCode::PackageScanAfterInitialize);

    std::filesystem::remove_all(root);
}
