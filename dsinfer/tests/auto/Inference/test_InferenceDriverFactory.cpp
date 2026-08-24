#include <memory>
#include <string>

#include <stdcorelib/plugin/plugin.h>
#include <stdcorelib/support/json.h>

#include <synthrt/Core/SynthUnit.h>

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
        explicit TestDriver(std::string name) : InferenceDriver(std::move(name)) {
        }

        std::string arch() const override {
            return "test-arch";
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
        explicit TestDriverPlugin(std::string driverName) : m_driverName(std::move(driverName)) {
        }

        srt::Expected<std::unique_ptr<ds::InferenceDriver>> create() override {
            return std::unique_ptr<ds::InferenceDriver>(new TestDriver(m_driverName));
        }

    private:
        std::string m_driverName;
    };

    stdc::json::Value manifest(std::string name) {
        return stdc::json::Object{
            {"iid",  ds::InferenceDriverPlugin::IID},
            {"name", std::move(name)               }
        };
    }

}

BOOST_AUTO_TEST_SUITE(test_InferenceDriverFactory)

BOOST_AUTO_TEST_CASE(test_DiscoversAndCreatesRuntimeService) {
    TestDriverPlugin plugin("test");
    ds::InferenceDriverFactory factory;
    factory.addRuntimePlugin(&plugin, manifest("test"));

    const auto names = factory.driverNames();
    BOOST_REQUIRE_EQUAL(names.size(), 1u);
    BOOST_CHECK_EQUAL(names[0], "test");

    auto result = factory.create("test");
    BOOST_REQUIRE(result);
    auto driver = result.take();
    BOOST_CHECK_EQUAL(driver->iid(), ds::InferenceDriver::IID);
    BOOST_CHECK_EQUAL(driver->name(), "test");
    BOOST_CHECK_EQUAL(driver->backend(), "test");
    BOOST_CHECK_EQUAL(driver->arch(), "test-arch");

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

BOOST_AUTO_TEST_SUITE_END()
