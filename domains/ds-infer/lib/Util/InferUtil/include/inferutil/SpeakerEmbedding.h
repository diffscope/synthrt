#ifndef DSINFER_INFERUTIL_SPEAKEREMBEDDING_H
#define DSINFER_INFERUTIL_SPEAKEREMBEDDING_H

#include <filesystem>
#include <map>

#include <synthrt/Core/Support/Expected.h>

#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::infer::inferutil {
    srt::core::Expected<std::vector<float>> loadSpeakerEmbedding(int hiddenSize,
                                                           const std::filesystem::path &path);

    srt::core::Expected<srt::core::NO<srt::core::ITensor>> preprocessSpeakerEmbeddingFrames(
        const std::vector<srt::svs::Api::Common::L1::InputSpeakerInfo> &speakers,
        const std::map<std::string, std::vector<float>> &embMap, int hiddenSize,
        double frameWidth, int64_t targetLength);
}

#endif // DSINFER_INFERUTIL_SPEAKEREMBEDDING_H