#ifndef DSINFER_INFERUTIL_SPEAKEREMBEDDING_H
#define DSINFER_INFERUTIL_SPEAKEREMBEDDING_H

#include <filesystem>
#include <map>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::inferutil {
    srt::Expected<std::vector<float>> loadSpeakerEmbedding(int hiddenSize,
                                                           const std::filesystem::path &path);

    srt::Expected<srt::NO<ITensor>> preprocessSpeakerEmbeddingFrames(
        const std::vector<Api::Common::L1::InputSpeakerInfo> &speakers,
        const std::map<std::string, std::vector<float>> &embMap, int hiddenSize,
        double frameWidth, int64_t targetLength);
}

#endif // DSINFER_INFERUTIL_SPEAKEREMBEDDING_H