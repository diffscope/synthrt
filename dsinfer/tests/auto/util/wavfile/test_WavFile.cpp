#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <wavfile/WavFile.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    struct TemporaryDirectory {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("support-tests-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryDirectory() {
            std::filesystem::create_directories(path);
        }
        ~TemporaryDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
        std::filesystem::path write(const std::string &name, const std::string &text) {
            const auto result = path / name;
            std::ofstream stream(result, std::ios::binary);
            stream << text;
            BOOST_REQUIRE(stream.good());
            return result;
        }
    };

}

BOOST_AUTO_TEST_CASE(float_wav_roundtrip_and_repeated_close) {
    TemporaryDirectory temporary;
    const auto path = temporary.path / "audio.wav";
    const ds::WavFile::DataFormat format{ds::WavFile::Container::RIFF,
                                         ds::WavFile::WaveFormat::IEEE_FLOAT, 1, 16000, 32};
    ds::WavFile writer;
    writer.close();
    BOOST_REQUIRE(writer.init_file_write(path, format));
    const std::vector<float> samples{-0.5f, 0, 0.5f};
    BOOST_CHECK_EQUAL(writer.write_pcm_frames(samples.size(), samples.data()), samples.size());
    writer.close();
    writer.close();
    std::ifstream stream(path, std::ios::binary);
    char riff[4]{};
    stream.read(riff, sizeof(riff));
    BOOST_CHECK_EQUAL(std::string(riff, 4), "RIFF");
    BOOST_CHECK_GE(std::filesystem::file_size(path), 44u + samples.size() * sizeof(float));
    BOOST_CHECK(!writer.init_file_write(temporary.path / "missing" / "audio.wav", format));
}
