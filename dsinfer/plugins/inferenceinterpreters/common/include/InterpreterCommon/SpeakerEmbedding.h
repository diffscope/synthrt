#ifndef DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H
#define DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H

#include <filesystem>
#include <fstream>

#include <stdcorelib/path.h>
#include <synthrt/Support/Expected.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::InterpreterCommon {
    inline srt::Expected<void>
        loadSpeakerEmbedding(const std::filesystem::path &path,
                             Api::Common::L1::SpeakerEmbedding::Vector &outBuffer) {
        namespace Co = Api::Common::L1;

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return srt::Error(srt::Error::FileNotFound,
                              "Failed to open file: " + stdc::path::to_utf8(path));
        }

        constexpr auto byteSize = Co::SpeakerEmbedding::Dimension * sizeof(float);
        file.read(reinterpret_cast<char *>(outBuffer.data()), byteSize);

        if (!file) {
            return srt::Error(srt::Error::SessionError, "File read failed: " + path.string());
        }

        if (file.gcount() != byteSize) {
            return srt::Error(srt::Error::SessionError, "File size is not exactly " +
                                                            std::to_string(byteSize) +
                                                            " bytes: " + path.string());
        }

        return srt::Expected<void>();
    }
}

#endif // DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H