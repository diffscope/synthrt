// Regression test: PinyinG2pTaskImplBase::initialize() must not deadlock
// when getConfig() is called while holding the internal shared_mutex
// exclusively. The fix replaces the getConfig() call with inline config
// building to avoid the unique_lock -> shared_lock self-deadlock on
// std::shared_mutex (MSVC SRWLOCK).
//
// Level: L1 (compiles PinyinG2pTaskImplBase.cpp + mock, no real ONNX, no
// real cpp-pinyin engine).
//
// Tags: [g2p][pinyin][regression]

#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/PinyinRes.h>
#include <inferutil/Verifier.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Task/G2pTask.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Module/Module_p.h>

#include "PinyinG2pTaskImplBase.h"

using namespace srt::g2p::plugins::Common;
using srt::core::Expected;
using srt::core::JsonArray;
using srt::core::JsonObject;
using srt::core::JsonValue;
using srt::core::ModuleSpec;

namespace {

    /// Test ModuleSpec with configurable path and manifest.
    class TestPinyinModuleSpec : public ModuleSpec {
    public:
        TestPinyinModuleSpec(const std::filesystem::path &basePath,
                              const JsonObject &manifestConfig)
            : ModuleSpec("test-pinyin-g2p") {
            auto *impl = static_cast<ModuleSpec::Impl *>(_impl.get());
            JsonObject manifest;
            manifest["id"] = JsonValue("test-pinyin-module");
            manifest["configuration"] = JsonValue(manifestConfig);
            impl->read(basePath, manifest);
        }
    };

    /// Mock PinyinG2pTask that never touches the real cpp-pinyin engine.
    class MockPinyinG2pTask : public PinyinG2pTaskImplBase {
    public:
        MockPinyinG2pTask(const ModuleSpec *spec, Config config)
            : PinyinG2pTaskImplBase(spec, std::move(config)) {}

    protected:
        Expected<void> onInitializeEngine() override {
            return {};
        }
        bool isEngineInitialized() const override {
            return true;
        }
        std::vector<Pinyin::PinyinRes> doHanziToPinyin(
            const std::vector<std::string> &) override {
            return {};
        }
    };

    std::filesystem::path createTempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-pinyin-init-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    JsonObject makeMinimalConfig(const std::string &dictPathKey) {
        // Build a verify entry array that only uses array-type (no regex,
        // no dict file -- avoids re2 and filesystem dependencies in L1 test).
        JsonObject arrayEntry;
        arrayEntry["type"] = JsonValue("array");
        JsonArray arrayValues;
        arrayValues.push_back(JsonValue("SP"));
        arrayValues.push_back(JsonValue("AP"));
        arrayValues.push_back(JsonValue("EP"));
        arrayEntry["value"] = JsonValue(arrayValues);
        arrayEntry["mode"] = JsonValue("copy");

        JsonArray verifyArray;
        verifyArray.push_back(JsonValue(arrayEntry));

        JsonObject config;
        config["verify"] = JsonValue(verifyArray);
        config[dictPathKey] = JsonValue("dict");
        return config;
    }

} // anonymous namespace

TEST_CASE("PinyinG2pTaskImplBase::initialize completes without deadlock",
          "[g2p][pinyin][regression]") {
    const auto baseDir = createTempDir();
    // Create the dictionary subdirectory (required by dictPath validation).
    std::filesystem::create_directories(baseDir / "dict");
    writeFile(baseDir / "dict" / "dummy.txt", "dummy content\n");

    const auto config = makeMinimalConfig("dictPath");

    TestPinyinModuleSpec spec(baseDir, config);
    MockPinyinG2pTask task(&spec, {"dictPath", "Mock"});

    std::promise<Expected<void>> resultPromise;
    std::thread worker([&] {
        resultPromise.set_value(task.initialize());
    });

    auto future = resultPromise.get_future();
    const auto status = future.wait_for(std::chrono::seconds(5));

    worker.join();

    REQUIRE(status == std::future_status::ready);
    auto initResult = future.get();
    REQUIRE(static_cast<bool>(initResult));

    std::filesystem::remove_all(baseDir);
}
