#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>

#include "../../lib/Core/Plugin/PluginFactory_p.h"

namespace {

    std::filesystem::path makeTempRoot(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-core-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

}

namespace {

    class TestPluginFactory : public srt::core::PluginFactory {
    public:
        std::vector<std::filesystem::path> sharedDirectories(const char *iid) const {
            std::shared_lock lock(_impl->m_plugins_mtx);
            const auto it = _impl->m_sharedDirs.find(iid);
            if (it == _impl->m_sharedDirs.end())
                return {};
            return {it->second.begin(), it->second.end()};
        }

        [[nodiscard]] bool isSharedDirectoryLoaded(const std::filesystem::path &path) const {
            std::shared_lock lock(_impl->m_plugins_mtx);
            return _impl->m_loadedSharedDirs.count(path.native()) != 0;
        }

        [[nodiscard]] bool isDirty(const char *iid) const {
            std::shared_lock lock(_impl->m_plugins_mtx);
            return _impl->m_pluginsDirty.count(iid) != 0;
        }

        [[nodiscard]] std::size_t preloadedLibraryCount() const {
            std::shared_lock lock(_impl->m_plugins_mtx);
            return _impl->m_preloadedLibraries.size();
        }
    };

}

TEST_CASE("PluginFactory derives one global shared dependency directory", "[plugin]") {
    const auto root = makeTempRoot("plugin-shared-path");
    TestPluginFactory factory;

    factory.addPluginPath("test.plugin", root / "plugins" / "module-a" / "category-a");
    factory.addPluginPath("test.plugin", root / "plugins" / "module-b" / "category-b");
    const auto sharedDirs = factory.sharedDirectories("test.plugin");

    REQUIRE(sharedDirs.size() == 1);
    CHECK(sharedDirs.front() == root / "plugins" / "_shared");

    std::filesystem::remove_all(root);
}

TEST_CASE("PluginFactory retries a shared directory that appears later", "[plugin]") {
    const auto root = makeTempRoot("plugin-shared-retry");
    const auto categoryDir = root / "plugins" / "module" / "category";
    const auto sharedDir = root / "plugins" / "_shared";
    TestPluginFactory factory;

    factory.addPluginPath("test.plugin", categoryDir);
    CHECK(factory.plugin("test.plugin", "missing") == nullptr);
    CHECK_FALSE(factory.isSharedDirectoryLoaded(sharedDir));
    CHECK(factory.isDirty("test.plugin"));

    std::filesystem::create_directories(sharedDir);
    CHECK(factory.plugin("test.plugin", "missing") == nullptr);
    CHECK(factory.isSharedDirectoryLoaded(sharedDir));
    CHECK_FALSE(factory.isDirty("test.plugin"));

    std::filesystem::remove_all(root);
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
