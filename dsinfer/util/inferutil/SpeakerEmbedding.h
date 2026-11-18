#ifndef DSINFER_INFERUTIL_SPEAKEREMBEDDING_H
#define DSINFER_INFERUTIL_SPEAKEREMBEDDING_H

#include <filesystem>
#include <map>
#include <string>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::inferutil {
    using SpeakerMapping = std::map<std::string, std::string>;

    /// Resolves a host speaker identifier through \a mapping.
    ///
    /// Returns \a name unchanged when the mapping has no entry for it.
    const std::string *resolveSpeakerName(const std::string &name,
                                          const SpeakerMapping &mapping) noexcept;

    srt::Expected<std::vector<float>> loadSpeakerEmbedding(int hiddenSize,
                                                           const std::filesystem::path &path);

    srt::Expected<std::shared_ptr<ITensor>> preprocessSpeakerEmbeddingFrames(
        const std::vector<Api::Common::L1::InputSpeakerInfo> &speakers,
        const SpeakerMapping &mapping, const std::map<std::string, std::vector<float>> &embMap,
        int hiddenSize, double frameWidth, int64_t targetLength);
}

#endif // DSINFER_INFERUTIL_SPEAKEREMBEDDING_H
