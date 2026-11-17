#ifndef DSINFER_TESTS_PLUGINS_SINGERPROVIDERS_DIFFSINGER_TESTSUPPORT_H
#define DSINFER_TESTS_PLUGINS_SINGERPROVIDERS_DIFFSINGER_TESTSUPPORT_H

#include <system_error>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/SVS/InferenceContrib.h>

#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace {

    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            static int sequence = 0;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_path =
                fs::temp_directory_path() / ("dsinfer-compatibility-auto-" + std::to_string(stamp) +
                                             "-" + std::to_string(sequence++));
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

    void writePackage(const fs::path &root, int acousticSampleRate, int vocoderSampleRate,
                      bool includeAcousticImport = true, bool includeVocoderImport = true,
                      const std::string &reservedPhonemes = "") {
        writeText(root / "desc.json",
                  R"({
                      "$version":"1.0",
                      "id":"compatibility-test",
                      "version":"1",
                      "runtimeLevel":1,
                      "contributions":{
                        "inference":[
                          {"id":"acoustic","path":"acoustic.json"},
                          {"id":"pitch","path":"pitch.json"},
                          {"id":"vocoder","path":"vocoder.json"}
                        ],
                        "singer":[{"id":"singer","path":"singer.json"}]
                      }
                  })");
        writeText(root / "phonemes.json", R"({"SP":0})");
        writeText(root / "acoustic.json",
                  R"({
                      "interface":"org.openvpi.dsinfer.inference.Acoustic",
                      "variant":"onnx",
                      "level":1,
                      "exports":{},
                      "configuration":{
                        "phonemes":"phonemes.json",
                        "model":"acoustic.onnx",
                        "sampleRate":)" +
                      std::to_string(acousticSampleRate) + R"(
                      }
                  })");
        writeText(root / "pitch.json",
                  R"({
                      "interface":"org.openvpi.dsinfer.inference.Pitch",
                      "variant":"onnx",
                      "level":1,
                      "exports":{},
                      "configuration":{
                        "phonemes":"phonemes.json",
                        "encoder":"pitch-encoder.onnx",
                        "predictor":"pitch-predictor.onnx",
                        "sampleRate":44100,
                        "hopSize":512
                      }
                  })");
        writeText(root / "vocoder.json",
                  R"({
                      "interface":"org.openvpi.dsinfer.inference.Vocoder",
                      "variant":"onnx",
                      "level":1,
                      "exports":{},
                      "configuration":{
                        "model":"vocoder.onnx",
                        "sampleRate":)" +
                      std::to_string(vocoderSampleRate) + R"(
                      }
                  })");

        std::string imports;
        if (includeAcousticImport) {
            imports += R"({"role":"singer/acoustic","ref":":inference/acoustic","options":{}})";
        }
        if (includeVocoderImport) {
            if (!imports.empty()) {
                imports += ',';
            }
            imports += R"({"role":"singer/vocoder","ref":":inference/vocoder","options":{}})";
        }
        writeText(root / "singer.json",
                  R"({
                      "interface":"org.openvpi.dsinfer.singer.DiffSinger",
                      "variant":"openvpi",
                      "level":1,
                      "exports":{},)" +
                      (reservedPhonemes.empty()
                           ? std::string()
                           : "\"reservedPhonemes\":" + reservedPhonemes + ",") +
                      R"(
                      "configuration":{"dict":"dictionary.txt"},
                      "imports":[)" +
                      imports + R"(]
                  })");
    }

    class ConfiguredSynthUnit final : public srt::SynthUnit {
    public:
        ConfiguredSynthUnit() {
            const std::array<fs::path, 1> inferencePaths = {DSINFER_TEST_INFERENCE_PLUGIN_PATH};
            const std::array<fs::path, 1> singerPaths = {DSINFER_TEST_SINGER_PLUGIN_PATH};
            setPluginPaths("inference", inferencePaths);
            setPluginPaths("singer", singerPaths);
        }
    };

    ConfiguredSynthUnit makeUnit() {
        return {};
    }

}


#endif // DSINFER_TESTS_PLUGINS_SINGERPROVIDERS_DIFFSINGER_TESTSUPPORT_H
