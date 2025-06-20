#ifndef DSINFER_WAVFILE_H
#define DSINFER_WAVFILE_H

#include <cstdint>
#include <filesystem>
#include <memory>

namespace ds {

    class WavFile {
    public:
        enum class Container { RIFF, RIFX, W64, RF64, AIFF };

        enum class WaveFormat : uint32_t {
            PCM = 0x1,
            ADPCM = 0x2,
            IEEE_FLOAT = 0x3,
            ALAW = 0x6,
            MULAW = 0x7,
            DVI_ADPCM = 0x11,
        };

        struct DataFormat {
            Container container;
            WaveFormat format;
            uint32_t channels;
            uint32_t sampleRate;
            uint32_t bitsPerSample;
        };

        WavFile();

        ~WavFile();

        bool init_file_write(const std::filesystem::path &path, const DataFormat &dataFormat);

        uint64_t write_pcm_frames(uint64_t frameCount, const void *pData);

        void close();

        WavFile(const WavFile &) = delete;
        WavFile &operator=(const WavFile &) = delete;

        WavFile(WavFile &&other) = default;
        WavFile &operator=(WavFile &&other) = default;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}
#endif // DSINFER_WAVFILE_H
