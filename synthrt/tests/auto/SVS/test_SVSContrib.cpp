#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>
#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/SingerPipelineExecInstance.h>
#include <synthrt/SVS/SingerProviderPlugin.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace {

    constexpr char testInferenceInterface[] = "com.example.Inference";
    constexpr char testInferenceVariant[] = "test";

    class TestInferenceExports : public srt::ContribExports {
    public:
        TestInferenceExports() : ContribExports(testInferenceInterface, testInferenceVariant, 1) {
        }
    };

    class TestInferenceConfiguration : public srt::ContribConfiguration {
    public:
        TestInferenceConfiguration()
            : ContribConfiguration(testInferenceInterface, testInferenceVariant, 1) {
        }
    };

    class TestInferenceImportOptions : public srt::ContribImportOptions {
    public:
        TestInferenceImportOptions()
            : ContribImportOptions(testInferenceInterface, testInferenceVariant, 1) {
        }
    };

    class TestInferenceInterpreter : public srt::InferenceInterpreter {
    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &) const override {
            return std::make_unique<TestInferenceExports>();
        }

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &) const override {
            return std::make_unique<TestInferenceConfiguration>();
        }

        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &, const srt::JsonValue &) const override {
            return std::make_unique<TestInferenceImportOptions>();
        }

        srt::Expected<std::unique_ptr<srt::InferenceExecInstance>>
            createInference(srt::InferenceSpec &, const srt::ContribImportOptions &,
                            const srt::InferenceRuntimeOptions &) override {
            return srt::Error(srt::Error::FeatureNotSupported,
                              "test interpreter does not create execution instances");
        }
    };

    class TestInferenceInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (interfaceName != testInferenceInterface || variant != testInferenceVariant ||
                level != 1) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unexpected test inference contract");
            }
            return std::make_unique<TestInferenceInterpreter>();
        }
    };

    STDC_EXPORT_STATIC_PLUGIN(TestInferenceInterpreterPlugin, srt::InferenceInterpreterPlugin::IID,
                              (stdc::json::Object{
                                  {"iid",      srt::InferenceInterpreterPlugin::IID              },
                                  {"name",     "test-inference-interpreter"                      },
                                  {"metadata",
                                   stdc::json::Object{
                                       {"interpreters", stdc::json::Array{stdc::json::Object{
                                                            {"interface", testInferenceInterface},
                                                            {"level", 1},
                                                            {"variant", testInferenceVariant}}}}}},
    }))

    class TestRuntimeOptions final : public srt::InferenceRuntimeOptions {
    public:
        TestRuntimeOptions() : InferenceRuntimeOptions("com.example.svs.Acoustic", "default", 1) {
        }
    };

    class TestSingerPipelineRuntimeOptions final : public srt::SingerPipelineRuntimeOptions {
    public:
        TestSingerPipelineRuntimeOptions()
            : SingerPipelineRuntimeOptions("com.example.svs.Singer", "test", 1) {
        }
    };

    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_path = fs::temp_directory_path() / ("synthrt-svs-auto-" + std::to_string(stamp));
            fs::create_directories(m_path);
        }

        ~TemporaryDirectory() {
            std::error_code error;
            fs::remove_all(m_path, error);
        }

        const fs::path &path() const {
            return m_path;
        }

    private:
        fs::path m_path;
    };

    void writeText(const fs::path &path, const std::string &text) {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        BOOST_REQUIRE(stream.is_open());
        stream << text;
        BOOST_REQUIRE(stream.good());
    }

}

BOOST_AUTO_TEST_SUITE(test_SVSContrib)

BOOST_AUTO_TEST_CASE(test_runtime_options_carry_contract_identity) {
    TestRuntimeOptions options;

    BOOST_CHECK_EQUAL(options.interface(), "com.example.svs.Acoustic");
    BOOST_CHECK_EQUAL(options.variant(), "default");
    BOOST_CHECK_EQUAL(options.level(), 1);

    TestSingerPipelineRuntimeOptions singerOptions;
    BOOST_CHECK_EQUAL(singerOptions.interface(), "com.example.svs.Singer");
    BOOST_CHECK_EQUAL(singerOptions.variant(), "test");
    BOOST_CHECK_EQUAL(singerOptions.level(), 1);
}

BOOST_AUTO_TEST_CASE(test_builtin_categories_parse_typed_data_only_specs) {
    TemporaryDirectory temporary;
    const auto root = temporary.path();
    writeText(root / "desc.json",
              R"({
                  "$version":"1.0",
                  "id":"voice",
                  "version":"1",
                  "runtimeLevel":1,
                  "contributions":{
                    "inference":[{"id":"acoustic","path":"modules/inference.json"}],
                    "singer":[{"id":"singer1","path":"modules/singer.json"}]
                  }
              })");
    writeText(root / "modules" / "inference.json",
              R"({
                  "interface":"com.example.svs.Acoustic",
                  "variant":"default",
                  "level":1,
                  "exports":{},
                  "configuration":{}
              })");
    writeText(root / "modules" / "singer.json",
              R"({
                  "interface":"com.example.svs.Singer",
                  "variant":"diffsinger",
                  "level":1,
                  "avatar":{"_":"../assets/singer1/avatar.png","zh-CN":"../assets/singer1/avatar-zh.png"},
                  "background":"../assets/singer1/background.png",
                  "demoAudio":"../assets/singer1/demo.wav",
                  "exports":{},
                  "configuration":{}
              })");

    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.category("inference"));
    BOOST_REQUIRE(unit.category("singer"));
    auto opened = unit.openPackage(root, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(opened);
    auto package = opened.take();

    auto *inferenceContribution = package.contribution("inference", "acoustic");
    BOOST_REQUIRE(inferenceContribution);
    auto *inference = inferenceContribution->as<srt::InferenceSpec>();
    BOOST_REQUIRE(inference);
    BOOST_CHECK(inference->declarationPath() == root / "modules" / "inference.json");
    BOOST_CHECK_EQUAL(inference->interface(), "com.example.svs.Acoustic");

    auto *singerContribution = package.contribution("singer", "singer1");
    BOOST_REQUIRE(singerContribution);
    auto *singer = singerContribution->as<srt::SingerSpec>();
    BOOST_REQUIRE(singer);
    BOOST_CHECK_EQUAL(singer->avatar().text(),
                      (root / "assets" / "singer1" / "avatar.png").string());
    BOOST_CHECK_EQUAL(singer->avatar().text("zh-CN"),
                      (root / "assets" / "singer1" / "avatar-zh.png").string());
    BOOST_CHECK_EQUAL(unit.category("inference")->interpreterIid(),
                      srt::InferenceInterpreterPlugin::IID);
    BOOST_CHECK_EQUAL(unit.category("singer")->interpreterIid(), srt::SingerProviderPlugin::IID);
}

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
    auto *consumer = package.contribution("inference", "consumer")->as<srt::InferenceSpec>();
    auto *producer = package.contribution("inference", "producer")->as<srt::InferenceSpec>();

    auto compatibility = consumer->validateCompatibilityWith(*producer);
    const auto compatibilityMessage =
        compatibility ? std::string() : compatibility.error().toString();
    BOOST_CHECK_MESSAGE(static_cast<bool>(compatibility), compatibilityMessage);
}

BOOST_AUTO_TEST_SUITE_END()
