#pragma once

#include <map>
#include <string>

#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

namespace ds::bank {
    struct InferenceInfo;
}

namespace ds::infer {

    /// SpeakerMapping - Per-inference mapping table from a singer's speaker id
    /// to the model's speaker id. Copied from ds::bank::SingerImportInfo during
    /// session assembly.
    struct DSINFER_EXPORT SpeakerMapping {
        /// singer speaker id -> model speaker id
        std::map<std::string, std::string> byId;
    };

    /// SpeakerMapper - Resolves a singer speaker to the model speaker id (and,
    /// in a later phase, the speaker embedding) expected by a given inference.
    ///
    /// \see 01-target-architecture.md section 3.2
    class DSINFER_EXPORT SpeakerMapper {
    public:
        SpeakerMapper();
        ~SpeakerMapper();

        /// Install (or replace) the speaker mapping for the given inference id.
        void setMapping(const std::string &inferenceId, SpeakerMapping mapping);

        /// Build a speaker mapping from an InferenceInfo's
        /// \c speakerEmbeddings table and install it for \p inferenceId.
        /// Each embedding entry's key is treated as the singer speaker id and
        /// the embedding path string is stored as the mapped value (real
        /// embedding loading is deferred to the driver layer).
        void loadFromInferenceInfo(const std::string &inferenceId,
                                   const ds::bank::InferenceInfo &info);

        /// Resolve a singer speaker to the model speaker id for the given
        /// inference. Returns an empty string when no mapping is registered
        /// for the inference, or when the singer speaker is not present in the
        /// table (caller treats empty as the model's default speaker).
        std::string resolve(const std::string &inferenceId,
                            const std::string &singerSpeaker) const;

    private:
        std::map<std::string, SpeakerMapping> m_mappings;
    };

} // namespace ds::infer
