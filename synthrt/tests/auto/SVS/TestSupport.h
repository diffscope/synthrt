#ifndef SYNTHRT_TESTS_SVS_TESTSUPPORT_H
#define SYNTHRT_TESTS_SVS_TESTSUPPORT_H

#include <system_error>

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
#include <synthrt/SVS/SingerPipelineExecutive.h>
#include <synthrt/SVS/SingerProviderPlugin.h>

#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace {

    constexpr char testInferenceInterface[] = "com.example.Inference";
    constexpr char testInferenceVariant[] = "test";

    class TestInferenceExports : public srt::ContribExports {
    public:
        TestInferenceExports() : ContribExports({testInferenceInterface, 1, testInferenceVariant}) {
        }
    };

    class TestInferenceConfiguration : public srt::ContribConfiguration {
    public:
        TestInferenceConfiguration()
            : ContribConfiguration({testInferenceInterface, 1, testInferenceVariant}) {
        }
    };

    class TestInferenceImportOptions : public srt::ContribImportOptions {
    public:
        TestInferenceImportOptions()
            : ContribImportOptions({testInferenceInterface, 1, testInferenceVariant}) {
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

        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &, const srt::ContribImportOptions &,
                            const srt::InferenceRuntimeOptions &) override {
            return srt::Error(srt::Error::FeatureNotSupported,
                              "test interpreter does not create executives");
        }
    };

    class TestInferenceInterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet !=
                srt::ContribContractTriplet(testInferenceInterface, 1, testInferenceVariant)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unexpected test inference contract");
            }
            return std::make_unique<TestInferenceInterpreter>();
        }
    };

    STDC_EXPORT_STATIC_PLUGIN(TestInferenceInterpreterPlugin, srt::InferenceInterpreterPlugin::IID,
                              (stdc::json::Object{
                                  {"name",         "test-inference-interpreter"                                                             },
                                  {"interpreters", stdc::json::Array{stdc::json::Object{
                                                       {"interface", testInferenceInterface},
                                                       {"level", 1},
                                                       {"variant", testInferenceVariant}}}},
    }))

    class TestRuntimeOptions final : public srt::InferenceRuntimeOptions {
    public:
        TestRuntimeOptions() : InferenceRuntimeOptions({"com.example.svs.Acoustic", 1, "default"}) {
        }
    };

    class TestSingerPipelineRuntimeOptions final : public srt::SingerPipelineRuntimeOptions {
    public:
        TestSingerPipelineRuntimeOptions()
            : SingerPipelineRuntimeOptions({"com.example.svs.Singer", 1, "test"}) {
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


#endif // SYNTHRT_TESTS_SVS_TESTSUPPORT_H
