#include <algorithm>
#include <memory>
#include <string>

#include <stdcorelib/plugin/pluginloader.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    /// The driver factory of a host, pointed at the drivers of dsinfer.
    struct Host {
        ds::InferenceDriverFactory factory;

        Host() {
            factory.addPluginPath(DSINFER_TEST_DRIVER_PLUGIN_PATH);
        }
    };

}

BOOST_AUTO_TEST_SUITE(test_InferenceDriverFactory_Plugin)

// The bundle is found by the backend its manifest names, under the directory dsinfer reports.
BOOST_AUTO_TEST_CASE(test_the_driver_is_found_by_its_backend) {
    Host host;
    const auto backends = host.factory.backends();
    BOOST_CHECK(std::find(backends.begin(), backends.end(), ds::Api::Onnx::API_NAME) !=
                backends.end());

    auto loader = host.factory.find(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(loader != nullptr);
    BOOST_CHECK(!loader->hasError());
    BOOST_CHECK_EQUAL(loader->filePath().parent_path().filename().string(), "onnx");
}

// A host registers the driver as a Runtime Service and an interpreter looks it up by the IID of
// drivers and the backend name. The name the driver gives itself must therefore be the backend its
// manifest declares, or the lookup finds nothing.
BOOST_AUTO_TEST_CASE(test_the_driver_names_itself_after_its_backend) {
    Host host;
    auto loader = host.factory.find(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(loader != nullptr);
    BOOST_CHECK_EQUAL(loader->metadata()["backend"].toString(), ds::Api::Onnx::API_NAME);

    auto created = host.factory.create(loader);
    BOOST_REQUIRE_MESSAGE(created, (created ? std::string() : created.error().message()));
    const auto driver = created.take();
    BOOST_REQUIRE(driver != nullptr);
    BOOST_CHECK_EQUAL(driver->iid(), ds::InferenceDriverPlugin::IID);
    BOOST_CHECK_EQUAL(driver->name(), ds::Api::Onnx::API_NAME);
}

// Every create() makes a driver of its own, since each one owns the runtime it is initialized with.
BOOST_AUTO_TEST_CASE(test_each_create_makes_a_separate_driver) {
    Host host;
    auto loader = host.factory.find(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(loader != nullptr);

    auto first = host.factory.create(loader);
    auto second = host.factory.create(loader);
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    BOOST_CHECK(first.value().get() != second.value().get());
}

BOOST_AUTO_TEST_SUITE_END()
