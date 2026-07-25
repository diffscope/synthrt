#pragma once

#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

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
    /// Mappings are keyed by (packageId, inferenceId) so that two packages
    /// that both define an inference with id "pitch" keep independent speaker
    /// tables (ARCH-06 cross-package stage sharing).
    ///
    /// \see 01-target-architecture.md section 3.2
    class DSINFER_EXPORT SpeakerMapper {
    public:
        SpeakerMapper();
        ~SpeakerMapper();

        /// Install (or replace) the speaker mapping for the given
        /// (packageId, inferenceId). The packageId isolates same-id inferences
        /// that originate from different packages.
        void setMapping(const std::string &packageId,
                        const std::string &inferenceId,
                        SpeakerMapping mapping);

        /// Build a speaker mapping from an InferenceInfo's
        /// \c speakerEmbeddings table and install it for
        /// (info.packageId, inferenceId). Each embedding entry's key is
        /// treated as the singer speaker id and the embedding path string is
        /// stored as the mapped value (real embedding loading is deferred to
        /// the driver layer).
        void loadFromInferenceInfo(const std::string &inferenceId,
                                   const ds::bank::InferenceInfo &info);

        /// Resolve a singer speaker to the model speaker id for the given
        /// (packageId, inferenceId). Returns an error when no mapping is
        /// registered for the key, or when the singer speaker is not present
        /// in the table.
        srt::core::Expected<std::string> resolve(const std::string &packageId,
                                                 const std::string &inferenceId,
                                                 const std::string &singerSpeaker) const;

    private:
        // Composite key: (packageId, inferenceId).
        using Key = std::pair<std::string, std::string>;
        std::map<Key, SpeakerMapping> m_mappings;

        // Guards m_mappings against concurrent access from inference threads
        // (resolve) and voicebank refresh threads (setMapping /
        // loadFromInferenceInfo). Readers take a shared_lock; writers take a
        // unique_lock (CODING-04).
        mutable std::shared_mutex m_mutex;
    };

} // namespace ds::infer
