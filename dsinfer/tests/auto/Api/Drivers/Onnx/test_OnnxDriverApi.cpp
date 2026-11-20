#include <memory>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Api = ds::Api::Onnx;

BOOST_AUTO_TEST_CASE(driver_and_session_payloads_carry_the_dispatch_contract) {
    Api::DriverInitArgs driver;
    Api::SessionOpenArgs open;
    Api::DriverExtension extension;
    const ds::InferenceDriverPayload *driverPayloads[] = {&driver, &open, &extension};
    for (const auto payload : driverPayloads) {
        BOOST_CHECK(payload->contract() == Api::Contract);
    }
    BOOST_CHECK(driver.ep == Api::ExecutionProvider::CPU);
    BOOST_CHECK_EQUAL(driver.deviceIndex, -1);
    BOOST_CHECK(!driver.runtimeApi);
    BOOST_CHECK(!open.useCpu);
    BOOST_CHECK(extension.runtimeApi.ortApi == nullptr);
    Api::SessionStartInput input;
    Api::SessionResult result;
    BOOST_CHECK(input.contract() == Api::Contract);
    BOOST_CHECK(result.contract() == Api::Contract);
    auto tensor = ds::Tensor::create(ds::ITensor::Float, {1});
    BOOST_REQUIRE(tensor);
    input.inputs.emplace("x", tensor.value());
    result.outputs.emplace("y", tensor.value());
    BOOST_CHECK(input.inputs.at("x") == result.outputs.at("y"));
}
