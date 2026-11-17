#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(test_runtime_services_are_owned_and_indexed_by_synth_unit) {
    TemporaryDirectory temporary;
    const auto root = writePackage(temporary.path(), "root", "root", "1");
    auto unit = makeUnit();

    auto onnx = std::make_unique<TestRuntimeService>("com.example.InferenceDriver", "onnx");
    auto onnxPointer = onnx.get();
    BOOST_REQUIRE(unit.addRuntimeService(std::move(onnx)));
    BOOST_REQUIRE(unit.addRuntimeService(
        std::make_unique<TestRuntimeService>("com.example.InferenceDriver", "remote")));
    BOOST_CHECK(unit.runtimeService("com.example.InferenceDriver", "onnx") == onnxPointer);
    BOOST_CHECK(&onnxPointer->synthUnit() == &unit);
    BOOST_CHECK_EQUAL(unit.runtimeServices("com.example.InferenceDriver").size(), 2u);
    BOOST_CHECK(unit.runtimeServices("com.example.Missing").empty());

    auto duplicate = unit.addRuntimeService(
        std::make_unique<TestRuntimeService>("com.example.InferenceDriver", "onnx"));
    BOOST_REQUIRE(!duplicate);
    BOOST_CHECK(duplicate.error().code() == srt::Error::InvalidArgument);

    auto invalidIid =
        unit.addRuntimeService(std::make_unique<TestRuntimeService>("invalid iid", "test"));
    BOOST_REQUIRE(!invalidIid);
    BOOST_CHECK(invalidIid.error().code() == srt::Error::InvalidArgument);
    auto invalidName = unit.addRuntimeService(
        std::make_unique<TestRuntimeService>("com.example.InferenceDriver", "invalid/name"));
    BOOST_REQUIRE(!invalidName);
    BOOST_CHECK(invalidName.error().code() == srt::Error::InvalidArgument);

    BOOST_REQUIRE(unit.openPackage(root, srt::SynthUnit::DataOnly));
    auto late = unit.addRuntimeService(
        std::make_unique<TestRuntimeService>("com.example.InferenceDriver", "late"));
    BOOST_REQUIRE(!late);
    BOOST_CHECK(late.error().code() == srt::Error::InvalidArgument);
}
