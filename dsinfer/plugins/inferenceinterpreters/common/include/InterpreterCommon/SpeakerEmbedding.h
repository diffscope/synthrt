#ifndef DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H
#define DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H

#include <filesystem>

#include <synthrt/Support/Expected.h>

namespace ds::InterpreterCommon {
    srt::Expected<std::vector<float>> loadSpeakerEmbedding(int hiddenSize,
                                                           const std::filesystem::path &path);
}

#endif // DSINFER_INTERPRETER_COMMON_SPEAKEREMBEDDING_H