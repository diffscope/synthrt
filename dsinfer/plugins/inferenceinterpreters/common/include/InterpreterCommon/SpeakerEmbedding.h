#ifndef DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H
#define DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H

#include <filesystem>
#include <fstream>

#include <stdcorelib/path.h>
#include <synthrt/Support/Expected.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::InterpreterCommon {
    inline srt::Expected<std::vector<float>>
        loadSpeakerEmbedding(int hiddenSize, const std::filesystem::path &path) {

        namespace Co = Api::Common::L1;

        if (hiddenSize <= 0) {
            return srt::Error(srt::Error::InvalidArgument, "hiddenSize must be a positive integer");
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return srt::Error(srt::Error::FileNotFound,
                              "Failed to open file: " + stdc::path::to_utf8(path));
        }

        const auto byteSize = hiddenSize * sizeof(float);
        std::vector<float> outVec(hiddenSize);
        file.read(reinterpret_cast<char *>(outVec.data()), byteSize);

        if (!file) {
            return srt::Error(srt::Error::SessionError, "File read failed: " + path.string());
        }

        if (file.gcount() != byteSize) {
            return srt::Error(srt::Error::SessionError, "File size is not exactly " +
                                                            std::to_string(byteSize) +
                                                            " bytes: " + path.string());
        }

        return outVec;
    }
}

#endif // DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H