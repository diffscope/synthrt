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
