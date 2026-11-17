#include "WavFile.h"

#ifndef DR_WAV_IMPLEMENTATION
#  define DR_WAV_IMPLEMENTATION
#endif
#include "dr_libs/dr_wav.h"

namespace ds {
    class WavFile::Impl {
    public:
        Impl() : wav{}, initialized(false) {
        }

        ~Impl() {
            close();
        }

        void close() {
            if (initialized) {
                drwav_uninit(&wav);
                initialized = false;
            }
        }

        drwav wav;
        bool initialized;
    };

    WavFile::WavFile() : _impl(std::make_unique<Impl>()) {
    }

    WavFile::~WavFile() = default;

    bool WavFile::init_file_write(const std::filesystem::path &path, const DataFormat &dataFormat) {
        auto &impl = *_impl;

        drwav_container container;
        switch (dataFormat.container) {
            case Container::RIFF:
                container = drwav_container_riff;
                break;
            case Container::RIFX:
                container = drwav_container_rifx;
                break;
            case Container::W64:
                container = drwav_container_w64;
                break;
            case Container::AIFF:
                container = drwav_container_aiff;
                break;
            case Container::RF64:
                container = drwav_container_rf64;
                break;
            default:
                container = drwav_container_riff;
                break;
        }
        drwav_data_format fmt{container, static_cast<uint32_t>(dataFormat.format),
                              dataFormat.channels, dataFormat.sampleRate, dataFormat.bitsPerSample};
#ifdef _WIN32
        auto path_str = path.wstring();
        auto ok = drwav_init_file_write_w(&impl.wav, path_str.c_str(), &fmt, nullptr);
#else
        auto path_str = path.string();
        auto ok = drwav_init_file_write(&impl.wav, path_str.c_str(), &fmt, nullptr);
#endif
        if (ok) {
            impl.initialized = true;
            return true;
        }
        return false;
    }

    uint64_t WavFile::write_pcm_frames(uint64_t frameCount, const void *pData) {
        auto &impl = *_impl;

        if (!impl.initialized) {
            return 0;
        }

        return drwav_write_pcm_frames(&impl.wav, frameCount, pData);
    }

    void WavFile::close() {
        auto &impl = *_impl;

        impl.close();
    }
}
