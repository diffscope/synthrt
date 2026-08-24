#include <array>
#include <filesystem>
#include <memory>
#include <string>

#include <stdcorelib/plugin/plugin.h>
#include <stdcorelib/support/json.h>

#include <synthrt/Core/SynthUnit.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>
#include <dsinfer/Inference/InferenceSession.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    class TestInitArgs : public ds::InferenceDriverInitArgs {
    public:
        TestInitArgs() : InferenceDriverInitArgs("test", 1) {
        }
    };

    class TestDriver : public ds::InferenceDriver {
    public:
        explicit TestDriver(const std::string &backend) : InferenceDriver(backend) {
        }

        srt::Expected<void> initialize(const ds::InferenceDriverInitArgs &args) override {
            if (args.type() != "test" || args.version() != 1) {
                return srt::Error(srt::Error::InvalidArgument, "unexpected test arguments");
            }
            m_initialized = true;
            return {};
        }

        std::unique_ptr<ds::InferenceSession> createSession() override {
            return {};
        }

        bool isInitialized() const noexcept {
            return m_initialized;
        }

    private:
        bool m_initialized = false;
    };

    class TestDriverPlugin : public ds::InferenceDriverPlugin {
    public:
        explicit TestDriverPlugin(std::string backend) : m_backend(std::move(backend)) {
        }

        srt::Expected<std::unique_ptr<ds::InferenceDriver>> create() override {
            return std::unique_ptr<ds::InferenceDriver>(new TestDriver(m_backend));
        }

    private:
        std::string m_backend;
    };

    stdc::json::Value manifest(std::string backend) {
        return stdc::json::Object{
            {"iid",      ds::InferenceDriverPlugin::IID                     },
            {"name",     "test-driver"                                      },
            {"metadata", stdc::json::Object{{"backend", std::move(backend)}}},
        };
    }

}

BOOST_AUTO_TEST_SUITE(test_InferenceDriverFactory)

BOOST_AUTO_TEST_CASE(test_DiscoversAndCreatesRuntimeService) {
    TestDriverPlugin plugin("test");
    ds::InferenceDriverFactory factory;
    factory.addRuntimePlugin(&plugin, manifest("test"));

    const auto backends = factory.backends();
    BOOST_REQUIRE_EQUAL(backends.size(), 1u);
    BOOST_CHECK_EQUAL(backends[0], "test");

    auto result = factory.create("test");
    BOOST_REQUIRE(result);
    auto driver = result.take();
    BOOST_CHECK_EQUAL(driver->iid(), ds::InferenceDriver::IID);
    BOOST_CHECK_EQUAL(driver->name(), "test");
    BOOST_CHECK_EQUAL(driver->backend(), "test");
    TestInitArgs args;
    BOOST_REQUIRE(driver->initialize(args));
    BOOST_CHECK(driver->as<TestDriver>()->isInitialized());

    auto *driverPointer = driver.get();
    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.addRuntimeService(std::move(driver)));
    BOOST_CHECK(unit.runtimeService(ds::InferenceDriver::IID, "test") == driverPointer);
    BOOST_CHECK(&driverPointer->synthUnit() == &unit);
}

BOOST_AUTO_TEST_CASE(test_RejectsMissingAndMismatchedDrivers) {
    TestDriverPlugin wrongPlugin("actual");
    ds::InferenceDriverFactory factory;

    auto missing = factory.create("missing");
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotFound);

    factory.addRuntimePlugin(&wrongPlugin, manifest("claimed"));
    auto mismatched = factory.create("claimed");
    BOOST_REQUIRE(!mismatched);
    BOOST_CHECK(mismatched.error().code() == srt::Error::InvalidFormat);

    auto invalid = factory.create("invalid/name");
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidArgument);
}

#ifdef DSINFER_TEST_DRIVER_PLUGIN_PATH
BOOST_AUTO_TEST_CASE(test_LoadsOnnxDriverBundle) {
    ds::InferenceDriverFactory factory;
    const std::array<std::filesystem::path, 1> pluginPaths{DSINFER_TEST_DRIVER_PLUGIN_PATH};
    factory.setPluginPaths(pluginPaths);

    const auto backends = factory.backends();
    BOOST_REQUIRE_EQUAL(backends.size(), 1u);
    BOOST_CHECK_EQUAL(backends.front(), ds::Api::Onnx::API_NAME);

    auto result = factory.create(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(result);
    auto driver = result.take();
    BOOST_CHECK_EQUAL(driver->backend(), "onnx");

    ds::Api::Onnx::DriverInitArgs initArgs;
    initArgs.runtimePath =
        pluginPaths.front() / ds::Api::Onnx::API_NAME / "runtimes" / "onnx" / "default";
    BOOST_REQUIRE(driver->initialize(initArgs));

    auto *driverPointer = driver.get();
    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.addRuntimeService(std::move(driver)));
    BOOST_CHECK(unit.runtimeService(ds::InferenceDriver::IID, ds::Api::Onnx::API_NAME) ==
                driverPointer);

    auto *extension = driverPointer->extension()->as<ds::Api::Onnx::DriverExtension>();
    auto externalResult = factory.create(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(externalResult);
    auto externalDriver = externalResult.take();
    ds::Api::Onnx::DriverInitArgs externalArgs;
    externalArgs.runtimeApi = extension->runtimeApi;
    BOOST_REQUIRE(externalDriver->initialize(externalArgs));
    auto *externalExtension = externalDriver->extension()->as<ds::Api::Onnx::DriverExtension>();
    BOOST_CHECK(externalExtension->runtimeApi.ortApiBase == extension->runtimeApi.ortApiBase);
    BOOST_CHECK(externalExtension->runtimeApi.ortApi == extension->runtimeApi.ortApi);
    BOOST_CHECK_EQUAL(externalExtension->runtimeApi.ortApiVersion,
                      extension->runtimeApi.ortApiVersion);

    auto session = driverPointer->createSession();
    BOOST_REQUIRE(session);
    BOOST_CHECK(!session->isOpen());
}
#endif

BOOST_AUTO_TEST_SUITE_END()
