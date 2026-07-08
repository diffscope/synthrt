#include "SpeakerMapper.h"

#include <stdcorelib/path.h>

#include <diffsinger/Bank/PackageManifest.h>

namespace ds::infer {

    SpeakerMapper::SpeakerMapper() = default;

    SpeakerMapper::~SpeakerMapper() = default;

    void SpeakerMapper::setMapping(const std::string &inferenceId, SpeakerMapping mapping) {
        m_mappings[inferenceId] = std::move(mapping);
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
        setMapping(inferenceId, std::move(mapping));
    }

    std::string SpeakerMapper::resolve(const std::string &inferenceId,
                                       const std::string &singerSpeaker) const {
        auto it = m_mappings.find(inferenceId);
        if (it == m_mappings.end()) {
            return {};
        }
        auto sit = it->second.byId.find(singerSpeaker);
        if (sit == it->second.byId.end()) {
            return {};
        }
        return sit->second;
    }

} // namespace ds::infer
