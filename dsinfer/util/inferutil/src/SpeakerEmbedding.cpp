#include <inferutil/SpeakerEmbedding.h>

#include <fstream>
#include <utility>

#include <stdcorelib/path.h>

#include <inferutil/Algorithm.h>

namespace ds::inferutil {
    namespace Co = Api::Common::L1;

    srt::Expected<std::vector<float>> loadSpeakerEmbedding(int hiddenSize,
                                                           const std::filesystem::path &path) {

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

    srt::Expected<srt::NO<ITensor>> preprocessSpeakerEmbeddingFrames(
        const std::vector<Api::Common::L1::InputSpeakerInfo> &speakers,
        const std::map<std::string, std::vector<float>> &embMap, int hiddenSize,
        double frameWidth, int64_t targetLength) {

        std::vector<int64_t> shape = {1, targetLength, hiddenSize};
        if (auto exp = Tensor::create(ITensor::Float, shape); exp) {
            // get tensor buffer
            auto tensor = exp.take();
            auto buffer = tensor->mutableData<float>();
            if (!buffer) {
                return srt::Error(srt::Error::SessionError, "failed to create spk_embed tensor");
            }

            // mix speaker embedding
            for (const auto &speaker : std::as_const(speakers)) {
                if (auto it_speaker = embMap.find(speaker.name); it_speaker != embMap.end()) {
                    const auto &embedding = it_speaker->second;
                    if (embedding.size() != hiddenSize) {
                        return srt::Error(
                            srt::Error::SessionError,
                            "speaker embedding vector length does not match hiddenSize");
                    }
                    auto resampled = resample(speaker.proportions, speaker.interval, frameWidth,
                                              targetLength, true);
                    for (size_t i = 0; i < resampled.size(); ++i) {
                        for (size_t j = 0; j < embedding.size(); ++j) {
                            float &val = buffer[i * embedding.size() + j];
                            val = std::fmaf(static_cast<float>(resampled[i]), embedding[j], val);
                        }
                    }
                } else {
                    return srt::Error(srt::Error::InvalidArgument,
                                      "invalid speaker name: " + speaker.name);
                }
            }
            return tensor;
        } else {
            return exp.takeError();
        }
    }
}