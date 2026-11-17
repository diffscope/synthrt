#ifndef DSINFER_TESTS_PLUGINS_INFERENCEINTERPRETERS_TESTSUPPORT_H
#define DSINFER_TESTS_PLUGINS_INFERENCEINTERPRETERS_TESTSUPPORT_H

#include <system_error>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <boost/test/unit_test.hpp>

namespace ds::test {
    namespace fs = std::filesystem;

    /// Loads a real interpreter with a small declaration and no inference backend.
    class DeclaredInference {
    public:
        DeclaredInference() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_root = fs::temp_directory_path() / ("dsinfer-declaration-" + std::to_string(stamp));
            fs::create_directories(m_root);
            write(
                "desc.json",
                R"({"$version":"1.0","id":"class-tests","version":"1","runtimeLevel":1,"contributions":{"inference":[{"id":"model","path":"model.json"}]}})");
            write("phonemes.json", R"({"SP":0,"a":1})");
            write(
                "model.json",
                std::string(R"({"interface":")") + Contract::API_INTERFACE +
                    R"(","level":1,"variant":"onnx","exports":{"predictions":["energy"]},"configuration":{"phonemes":"phonemes.json","model":"model.onnx","encoder":"encoder.onnx","predictor":"predictor.onnx","sampleRate":44100,"hopSize":512,"frameWidth":0.01}})");
            auto opened = m_unit.openPackage(m_root, srt::SynthUnit::Load);
            BOOST_REQUIRE_MESSAGE(opened, (opened ? std::string() : opened.error().message()));
            m_package = opened.take();
        }
        ~DeclaredInference() {
            m_package.reset();
            std::error_code error;
            fs::remove_all(m_root, error);
        }
        srt::InferenceSpec &spec() {
            auto result = m_package.contribution(srt::InferenceCategory::NAME, "model");
            BOOST_REQUIRE(result);
            return *result->as<srt::InferenceSpec>();
        }

    private:
        void write(const char *name, const std::string &text) {
            std::ofstream stream(m_root / name, std::ios::binary);
            stream << text;
            BOOST_REQUIRE(stream.good());
        }
        fs::path m_root;
        srt::SynthUnit m_unit;
        srt::PackageHandle m_package;
    };
}

namespace {
    class InterpreterPlugin : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            if (triplet != srt::ContribTriplet(Contract::API_INTERFACE, Contract::API_LEVEL,
                                               Contract::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument, "unexpected test contract");
            }
            return std::make_unique<TestInterpreter>();
        }
    };
    STDC_EXPORT_STATIC_PLUGIN(
        InterpreterPlugin, srt::InferenceInterpreterPlugin::IID,
        (srt::JsonObject{
            {"name",         "class-test-interpreter"                                                                       },
            {"interpreters", srt::JsonArray{srt::JsonObject{{"interface", Contract::API_INTERFACE},
                                                            {"level", Contract::API_LEVEL},
                                                            {"variant", Contract::API_VARIANT}}}}
    }))
}

#endif // DSINFER_TESTS_PLUGINS_INFERENCEINTERPRETERS_TESTSUPPORT_H
