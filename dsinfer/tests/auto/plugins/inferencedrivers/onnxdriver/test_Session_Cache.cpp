#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include <dsinfer/Support/ErrorCode.h>

#include <onnxdriver/Runtime/DriverContext.h>
#include <onnxdriver/Runtime/OnnxRuntime.h>
#include <onnxdriver/Session/Session.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;
namespace Onnx = ds::Api::Onnx;

using ds::onnxdriver::DriverContext;
using ds::onnxdriver::OnnxRuntime;
using ds::onnxdriver::Session;
using ds::onnxdriver::SessionSystem;

namespace {

    /// A driver context on the CPU, as OnnxDriver::initialize() creates one, and a scratch
    /// directory that holds a model, a byte-identical copy of it at another path, and a different
    /// model.
    struct Driver {
        std::shared_ptr<DriverContext> context;
        fs::path directory;
        fs::path model;
        fs::path copy;
        fs::path other;

        Driver() {
            auto runtime = OnnxRuntime::load(fs::path(DSINFER_TEST_ORT_RUNTIME_DIR) /
                                             ONNXRUNTIME_DYLIB_FILENAME);
            BOOST_REQUIRE_MESSAGE(runtime,
                                  "ONNX Runtime does not load from " +
                                      std::string(DSINFER_TEST_ORT_RUNTIME_DIR) + ": " +
                                      (runtime ? std::string() : runtime.error().message()));
            context =
                std::make_shared<DriverContext>(runtime.take(), Onnx::ExecutionProvider::CPU, 0);

            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            directory =
                fs::temp_directory_path() / ("dsinfer-session-cache-" + std::to_string(stamp));
            fs::create_directories(directory);

            const fs::path resources = TEST_RESOURCE_DIRECTORY;
            model = directory / "model.onnx";
            copy = directory / "copy.onnx";
            other = directory / "other.onnx";
            fs::copy_file(resources / "mixed_type_ops.onnx", model);
            fs::copy_file(resources / "mixed_type_ops.onnx", copy);
            fs::copy_file(resources / "duration_word_encoder.onnx", other);
        }

        ~Driver() {
            std::error_code ignored;
            fs::remove_all(directory, ignored);
        }

        SessionSystem &cache() {
            return context->sessionSystem();
        }

        std::unique_ptr<Session> open(const fs::path &path, bool useCpu = false) {
            auto session = std::make_unique<Session>(context);
            Onnx::SessionOpenArgs args;
            args.useCpu = useCpu;
            auto opened = session->open(path, args);
            BOOST_REQUIRE_MESSAGE(opened, path.string() + " does not open: " +
                                              (opened ? std::string() : opened.error().message()));
            return session;
        }

        bool recordsPath(const fs::path &path) {
            return cache().pathMap.count(fs::canonical(path).native()) != 0;
        }
    };

    size_t references(const SessionSystem::ImageGroup &group, bool useCpu) {
        const auto it = group.images.find(useCpu);
        return it == group.images.end() ? 0 : it->second.referenceCount;
    }

}

BOOST_AUTO_TEST_SUITE(test_Session_Cache)

BOOST_AUTO_TEST_CASE(test_one_path_opened_twice_shares_one_image) {
    Driver driver;
    auto first = driver.open(driver.model);
    auto second = driver.open(driver.model);

    BOOST_REQUIRE_EQUAL(driver.cache().imageList.size(), 1u);
    const auto &group = driver.cache().imageList.front();
    BOOST_CHECK_EQUAL(group.images.size(), 1u);
    BOOST_CHECK_EQUAL(references(group, false), 2u);
}

// Two paths with the same content are one model, found by the hash and the size of the file.
// The group keeps the path it was created for, and each session keeps its own.
BOOST_AUTO_TEST_CASE(test_a_copy_at_another_path_shares_the_image) {
    Driver driver;
    auto original = driver.open(driver.model);
    auto copied = driver.open(driver.copy);

    BOOST_REQUIRE_EQUAL(driver.cache().imageList.size(), 1u);
    BOOST_CHECK_EQUAL(references(driver.cache().imageList.front(), false), 2u);
    BOOST_CHECK(driver.recordsPath(driver.model));
    BOOST_CHECK(!driver.recordsPath(driver.copy));
    BOOST_CHECK(copied->path() == fs::canonical(driver.copy));
}

BOOST_AUTO_TEST_CASE(test_a_different_model_has_an_image_of_its_own) {
    Driver driver;
    auto first = driver.open(driver.model);
    auto second = driver.open(driver.other);

    BOOST_CHECK_EQUAL(driver.cache().imageList.size(), 2u);
    BOOST_CHECK_EQUAL(driver.cache().pathMap.size(), 2u);
    BOOST_CHECK_EQUAL(driver.cache().hashSizeMap.size(), 2u);
}

// A session that asks for the CPU needs a session created for the CPU, so one model holds an
// image for each preference.
BOOST_AUTO_TEST_CASE(test_the_cpu_preference_keeps_a_second_image_of_the_model) {
    Driver driver;
    auto preferred = driver.open(driver.model, false);
    auto cpu = driver.open(driver.model, true);

    BOOST_REQUIRE_EQUAL(driver.cache().imageList.size(), 1u);
    const auto &group = driver.cache().imageList.front();
    BOOST_CHECK_EQUAL(group.images.size(), 2u);
    BOOST_CHECK_EQUAL(references(group, false), 1u);
    BOOST_CHECK_EQUAL(references(group, true), 1u);
}

BOOST_AUTO_TEST_CASE(test_the_last_close_releases_the_image_and_its_entries) {
    Driver driver;
    auto first = driver.open(driver.model);
    auto second = driver.open(driver.model);
    auto other = driver.open(driver.other);

    BOOST_REQUIRE(first->close());
    BOOST_REQUIRE_EQUAL(driver.cache().imageList.size(), 2u);
    BOOST_CHECK(driver.recordsPath(driver.model));

    BOOST_REQUIRE(second->close());
    BOOST_CHECK_EQUAL(driver.cache().imageList.size(), 1u);
    BOOST_CHECK(!driver.recordsPath(driver.model));

    BOOST_REQUIRE(other->close());
    BOOST_CHECK(driver.cache().imageList.empty());
    BOOST_CHECK(driver.cache().pathMap.empty());
    BOOST_CHECK(driver.cache().hashSizeMap.empty());
}

// The session that created a group can close while another session still uses it. The group then
// stays, still keyed by the path it was created for, and a later open of that path finds it.
BOOST_AUTO_TEST_CASE(test_the_group_outlives_the_session_that_created_it) {
    Driver driver;
    auto creator = driver.open(driver.model);
    auto copied = driver.open(driver.copy);

    BOOST_REQUIRE(creator->close());
    BOOST_REQUIRE_EQUAL(driver.cache().imageList.size(), 1u);
    BOOST_CHECK_EQUAL(references(driver.cache().imageList.front(), false), 1u);
    BOOST_CHECK(driver.recordsPath(driver.model));

    auto reopened = driver.open(driver.model);
    BOOST_REQUIRE_EQUAL(driver.cache().imageList.size(), 1u);
    BOOST_CHECK_EQUAL(references(driver.cache().imageList.front(), false), 2u);
}

BOOST_AUTO_TEST_CASE(test_a_model_opened_again_after_release_gets_a_new_image) {
    Driver driver;
    auto session = driver.open(driver.model);
    BOOST_REQUIRE(session->close());
    BOOST_REQUIRE(driver.cache().imageList.empty());

    auto again = driver.open(driver.model);
    BOOST_REQUIRE_EQUAL(driver.cache().imageList.size(), 1u);
    BOOST_CHECK_EQUAL(references(driver.cache().imageList.front(), false), 1u);
}

BOOST_AUTO_TEST_CASE(test_a_session_refuses_what_its_state_does_not_allow) {
    Driver driver;

    Session closed(driver.context);
    auto notOpen = closed.close();
    BOOST_REQUIRE(!notOpen);
    BOOST_CHECK(notOpen.error().code() == ds::ErrorCode::NotInitialized);

    auto missing = closed.open(driver.directory / "missing.onnx", Onnx::SessionOpenArgs());
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotOpen);
    BOOST_CHECK(driver.cache().imageList.empty());

    auto session = driver.open(driver.model);
    auto twice = session->open(driver.model, Onnx::SessionOpenArgs());
    BOOST_REQUIRE(!twice);
    BOOST_CHECK(twice.error().code() == ds::ErrorCode::AlreadyOpen);
    BOOST_CHECK_EQUAL(references(driver.cache().imageList.front(), false), 1u);
}

BOOST_AUTO_TEST_SUITE_END()
