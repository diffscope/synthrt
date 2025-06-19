#ifndef DSINFER_WAVFILE_H
#define DSINFER_WAVFILE_H

#include <cstdint>
#include <filesystem>
#include <memory>

#ifndef DR_WAV_IMPLEMENTATION
#  define DR_WAV_IMPLEMENTATION
#endif
#include "dr_wav.h"

class WavFile {
public:
    WavFile() : m_wav(std::make_unique<drwav>()) {
    }

    ~WavFile() {
        close();
    }

    drwav_bool32 init_file_write(const std::filesystem::path &path, const drwav_data_format *pFormat,
                         const drwav_allocation_callbacks *pAllocationCallbacks) {
        if (!m_wav) {
            return false;
        }
#ifdef _WIN32
        auto path_str = path.wstring();
        return drwav_init_file_write_w(m_wav.get(), path_str.c_str(), pFormat, pAllocationCallbacks);
#else
        auto path_str = path.string();
        return drwav_init_file_write(m_wav.get(), path_str.c_str(), pFormat, pAllocationCallbacks);
#endif
    }

    drwav_uint64 write_pcm_frames(uint64_t frameCount, const void *pData) {
        if (!m_wav) {
            return 0;
        }
        return drwav_write_pcm_frames(m_wav.get(), frameCount, pData);
    }

    drwav *get() {
        return m_wav.get();
    }

    void close() {
        if (m_wav) {
            drwav_uninit(m_wav.get());
            m_wav.reset();
        }
    }

    WavFile(const WavFile &) = delete;
    WavFile &operator=(const WavFile &) = delete;

    WavFile(WavFile &&other) = delete;

    WavFile &operator=(WavFile &&other) = delete;


private:
    std::unique_ptr<drwav> m_wav;
};

#endif // DSINFER_WAVFILE_H