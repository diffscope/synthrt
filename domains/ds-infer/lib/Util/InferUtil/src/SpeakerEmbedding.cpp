#include <inferutil/SpeakerEmbedding.h>

#include <cmath>
#include <fstream>
#include <utility>

#include <stdcorelib/path.h>

#include <inferutil/Algorithm.h>

namespace ds::infer::inferutil {
    namespace Co = srt::svs::Api::Common::L1;

    srt::core::Expected<std::vector<float>> loadSpeakerEmbedding(int hiddenSize,
                                                           const std::filesystem::path &path) {

        if (hiddenSize <= 0) {
            return srt::core::Error(srt::core::Error::InvalidArgument, "hiddenSize must be a positive integer");
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return srt::core::Error(srt::core::Error::FileNotOpen,
                              "Failed to open file: " + stdc::path::to_utf8(path));
        }

        const auto byteSize = hiddenSize * sizeof(float);
        std::vector<float> outVec(hiddenSize);
        file.read(reinterpret_cast<char *>(outVec.data()), byteSize);

        if (!file) {
            return srt::core::Error(srt::core::Error::SessionError, "File read failed: " + stdc::path::to_utf8(path));
        }

        if (file.gcount() != byteSize) {
            return srt::core::Error(srt::core::Error::SessionError, "File size is not exactly " +
                                                            std::to_string(byteSize) +
                                                            " bytes: " + stdc::path::to_utf8(path));
        }

        return outVec;
    }

    srt::core::Expected<srt::core::NO<srt::core::ITensor>> preprocessSpeakerEmbeddingFrames(
        const std::vector<srt::svs::Api::Common::L1::InputSpeakerInfo> &speakers,
        const std::map<std::string, std::vector<float>> &embMap, int hiddenSize,
        double frameWidth, int64_t targetLength) {

        // BF-42: validate frameWidth (defense-in-depth — callers already
        // check, but resample() now returns empty for non-positive
        // targetTimestep, which would silently skip the speaker).
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                                    "preprocessSpeakerEmbeddingFrames: frameWidth must be positive");
        }

        std::vector<int64_t> shape = {1, targetLength, hiddenSize};
        if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, shape); exp) {
            // get tensor buffer
            auto tensor = exp.take();
            auto buffer = tensor->mutableData<float>();
            if (!buffer) {
                return srt::core::Error(srt::core::Error::SessionError, "failed to create spk_embed tensor");
            }

            // mix speaker embedding
            for (const auto &speaker : std::as_const(speakers)) {
                const std::vector<float> *embeddingPtr = nullptr;

                // 1. Try voice bank lookup by name
                if (auto it_speaker = embMap.find(speaker.name); it_speaker != embMap.end()) {
                    embeddingPtr = &it_speaker->second;
                }
                // 2. Fall back to inline embedding (allows custom/undefined speakers)
                else if (!speaker.embedding.empty()) {
                    embeddingPtr = &speaker.embedding;
                } else {
                    return srt::core::Error(
                        srt::core::Error::InvalidArgument,
                        "speaker \"" + speaker.name +
                            "\" not found in voice bank and no inline embedding provided");
                }

                const auto &embedding = *embeddingPtr;
                if (embedding.size() != static_cast<size_t>(hiddenSize)) {
                    return srt::core::Error(
                        srt::core::Error::SessionError,
                        "speaker embedding vector length does not match hiddenSize");
                }
                // BF-34: Validate proportions before resampling. When
                // proportions is empty, or when interval is 0 with
                // multiple proportions, resample() silently returns an
                // empty vector and the speaker is skipped without error,
                // violating ROBUST-05. A single-element proportions with
                // interval 0 is valid (static speaker, broadcast to all
                // frames by resample).
                if (speaker.proportions.empty()) {
                    return srt::core::Error(
                        srt::core::Error::InvalidArgument,
                        "speaker \"" + speaker.name +
                            "\" has empty proportions");
                }
                if (speaker.interval <= 0 && speaker.proportions.size() > 1) {
                    return srt::core::Error(
                        srt::core::Error::InvalidArgument,
                        "speaker \"" + speaker.name +
                            "\" has multiple proportions but interval is not positive");
                }
                auto resampled = resample(speaker.proportions, speaker.interval, frameWidth,
                                          targetLength, true);
                // After the guards above, resampled is non-empty as long
                // as targetLength > 0 and frameWidth > 0 (caller's
                // responsibility). If targetLength is 0 the loop is a
                // no-op, which is correct (empty output).
                for (size_t i = 0; i < resampled.size(); ++i) {
                    for (size_t j = 0; j < embedding.size(); ++j) {
                        float &val = buffer[i * embedding.size() + j];
                        val = std::fmaf(static_cast<float>(resampled[i]), embedding[j], val);
                    }
                }
            }
            return tensor;
        } else {
            return exp.takeError();
        }
    }
}