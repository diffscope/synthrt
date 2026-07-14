#include "SpeakerMapper.h"

#include <stdcorelib/path.h>

#include <diffsinger/Bank/PackageManifest.h>

namespace ds::infer {

    SpeakerMapper::SpeakerMapper() = default;

    SpeakerMapper::~SpeakerMapper() = default;

    void SpeakerMapper::setMapping(const std::string &packageId,
                                   const std::string &inferenceId,
                                   SpeakerMapping mapping) {
        // Composite key (packageId, inferenceId) isolates same-id inferences
        // from different packages (ARCH-06). Without packageId, the second
        // package's setMapping would silently overwrite the first package's
        // table.
        m_mappings[{packageId, inferenceId}] = std::move(mapping);
    }

    void SpeakerMapper::loadFromInferenceInfo(const std::string &inferenceId,
                                              const ds::bank::InferenceInfo &info) {
        SpeakerMapping mapping;
        for (const auto &entry : info.speakerEmbeddings) {
            // Key is the singer speaker id; store the embedding path string as
            // the mapped value (real embedding loading is deferred to the
            // driver layer).
            mapping.byId[entry.first] = stdc::path::to_utf8(entry.second);
        }
        // info.packageId is stamped by PackageParser from the owning manifest.
        setMapping(info.packageId, inferenceId, std::move(mapping));
    }

    srt::core::Expected<std::string>
        SpeakerMapper::resolve(const std::string &packageId,
                               const std::string &inferenceId,
                               const std::string &singerSpeaker) const {
        auto it = m_mappings.find({packageId, inferenceId});
        if (it == m_mappings.end()) {
            return srt::core::Error(
                srt::core::ErrorCode::InferenceSpeakerNotFound,
                "no speaker mapping registered for inference: " + inferenceId +
                    " in package: " + packageId);
        }
        auto sit = it->second.byId.find(singerSpeaker);
        if (sit == it->second.byId.end()) {
            return srt::core::Error(
                srt::core::ErrorCode::InferenceSpeakerNotFound,
                "speaker not found in mapping: " + singerSpeaker);
        }
        return sit->second;
    }

} // namespace ds::infer
