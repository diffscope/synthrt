#include <synthrt/Core/ContribImportBinding.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(test_inference_compatibility_defaults_to_supported) {
    TemporaryDirectory temporary;
    const auto root = temporary.path();
    writeText(root / "desc.json",
              R"({
                  "$version":"1.0",
                  "id":"compatibility-test",
                  "version":"1",
                  "runtimeLevel":1,
                  "contributions":{
                    "inference":[
                      {"id":"consumer","path":"consumer.json"},
                      {"id":"producer","path":"producer.json"}
                    ]
                  }
              })");
    const std::string declaration = R"({"interface":")" + std::string(testInferenceInterface) +
                                    R"(","variant":")" + testInferenceVariant +
                                    R"(","level":1,"exports":{},"configuration":{}})";
    writeText(root / "consumer.json", declaration);
    writeText(root / "producer.json", declaration);

    srt::SynthUnit unit;
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE(opened);
    auto package = opened.take();
    auto consumer = package.contribution("inference", "consumer")->as<srt::InferenceSpec>();
    auto producer = package.contribution("inference", "producer")->as<srt::InferenceSpec>();

    auto compatibility = consumer->validateCompatibilityWith(*producer);
    const auto compatibilityMessage =
        compatibility ? std::string() : compatibility.error().toString();
    BOOST_CHECK_MESSAGE(static_cast<bool>(compatibility), compatibilityMessage);
}

BOOST_AUTO_TEST_CASE(default_import_bindings_activate_and_factories_preserve_contract_errors) {
    TemporaryDirectory temporary;
    const auto root = temporary.path();
    writeText(root / "desc.json", R"({
        "$version":"1.0","id":"bindings","version":"1","runtimeLevel":1,
        "contributions":{"inference":[
            {"id":"consumer","path":"consumer.json"},
            {"id":"producer","path":"producer.json"}]}})");
    const std::string declaration = R"({"interface":")" + std::string(testInferenceInterface) +
                                    R"(","variant":")" + testInferenceVariant +
                                    R"(","level":1,"exports":{},"configuration":{})";
    writeText(root / "producer.json", declaration + "}");
    writeText(root / "consumer.json",
              declaration + R"(,"imports":[{"role":"model","ref":":inference/producer"}]})");
    srt::SynthUnit unit;
    auto loaded = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE(loaded);
    auto package = loaded.take();
    auto consumer = package.contribution("inference", "consumer");
    auto producer = package.contribution("inference", "producer");
    BOOST_REQUIRE(consumer);
    BOOST_REQUIRE(producer);
    auto imported = consumer->findImport("model");
    BOOST_REQUIRE(imported);
    auto binding = imported->binding();
    BOOST_REQUIRE(binding);
    BOOST_CHECK(binding->state() == srt::ContribImportBinding::Active);
    BOOST_CHECK(&binding->importer() == consumer);
    BOOST_CHECK(&binding->target() == producer);
    BOOST_CHECK(&binding->declaration() == &consumer->imports()[0]);
    BOOST_CHECK(binding->options().triplet() == producer->triplet());
    auto factory = imported->executiveFactory();
    BOOST_REQUIRE(factory);
    auto mismatched = factory->create(TestRuntimeOptions());
    BOOST_REQUIRE(!mismatched);
    BOOST_CHECK(mismatched.error().code() == srt::Error::InvalidArgument);
    class RuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        RuntimeOptions()
            : InferenceRuntimeOptions({testInferenceInterface, 1, testInferenceVariant}) {
        }
    };
    auto refused = factory->create(RuntimeOptions());
    BOOST_REQUIRE(!refused);
    BOOST_CHECK(refused.error().code() == srt::Error::FeatureNotSupported);
    BOOST_CHECK(refused.error().message().find("does not create executives") != std::string::npos);
    package.reset();
    BOOST_CHECK(unit.loadedPackages().empty());
    BOOST_CHECK(unit.category("inference")->contributions().empty());
}
