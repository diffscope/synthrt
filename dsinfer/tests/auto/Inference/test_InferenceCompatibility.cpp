#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <synthrt/Core/SynthUnit.h>

#define BOOST_TEST_MAIN
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
                      bool includeAcousticImport = true, bool includeVocoderImport = true) {
        writeText(root / "desc.json",
                  R"({
                      "$version":"1.0",
                      "id":"compatibility-test",
                      "version":"1",
                      "runtimeLevel":1,
                      "contributions":{
                        "inference":[
                          {"id":"acoustic","path":"acoustic.json"},
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
            imports += R"({"role":"acoustic","ref":":inference/acoustic","options":{}})";
        }
        if (includeVocoderImport) {
            if (!imports.empty()) {
                imports += ',';
            }
            imports += R"({"role":"vocoder","ref":":inference/vocoder","options":{}})";
        }
        writeText(root / "singer.json",
                  R"({
                      "interface":"org.openvpi.dsinfer.singer.DiffSinger",
                      "variant":"openvpi",
                      "level":1,
                      "exports":{},
                      "configuration":{"dict":"dictionary.txt"},
                      "imports":[)" +
                      imports + R"(]
                  })");
    }

    srt::SynthUnit makeUnit() {
        srt::SynthUnit unit;
        const std::array<fs::path, 1> inferencePaths = {DSINFER_TEST_INFERENCE_PLUGIN_PATH};
        const std::array<fs::path, 1> singerPaths = {DSINFER_TEST_SINGER_PLUGIN_PATH};
        unit.setPluginPaths("inference", inferencePaths);
        unit.setPluginPaths("singer", singerPaths);
        return unit;
    }

}

BOOST_AUTO_TEST_SUITE(test_InferenceCompatibility)

BOOST_AUTO_TEST_CASE(test_compatible_acoustic_and_vocoder_commit) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100);
    auto unit = makeUnit();

    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);

    BOOST_REQUIRE(opened);
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 1u);
}

BOOST_AUTO_TEST_CASE(test_incompatible_acoustic_and_vocoder_do_not_commit) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 48000);
    auto unit = makeUnit();

    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);

    BOOST_CHECK(!opened);
    BOOST_CHECK(opened.error().toString().find("sampleRate") != std::string::npos);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(test_missing_required_inference_role_does_not_commit) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100, false, true);
    auto unit = makeUnit();

    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);

    BOOST_CHECK(!opened);
    BOOST_CHECK(opened.error().toString().find("acoustic inference import") != std::string::npos);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_SUITE_END()
