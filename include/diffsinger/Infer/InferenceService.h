#pragma once

#include <filesystem>

#include <diffsinger/Infer/InferenceRequest.h>
#include <diffsinger/Infer/InferenceResult.h>
#include <diffsinger/Infer/StageKind.h>
#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

#include <synthrt/SVS/InferenceContrib.h>

namespace ds::infer {

    /// StageSpec — A single pipeline stage: an InferenceSpec (from a loaded
    /// singer import) plus its ImportOptions. The spec is not owned by this
    /// struct; the caller must keep it alive for the service's lifetime.
    struct DSINFER_EXPORT StageSpec {
        StageKind kind = StageKind::Duration;           ///< v2: stage identifier
        srt::svs::InferenceSpec *spec = nullptr;
        srt::core::NO<srt::svs::InferenceImportOptions> options;
        std::filesystem::path packageDirectory;         ///< v2: source package directory
    };

    /// StageSet — Named collection of the 5 inference stage specifications.
    /// Replaces array-by-index with self-documenting named fields.
    /// spec pointers remain valid as long as Runtime holds the package open.
    struct DSINFER_EXPORT StageSet {
        StageSpec duration;
        StageSpec pitch;
        StageSpec variance;
        StageSpec acoustic;
        StageSpec vocoder;

        /// Find a StageSpec by StageKind. Returns nullptr if not set.
        const StageSpec *find(StageKind kind) const noexcept;
    };

    /// InferenceService - Orchestrates the 5-stage DiffSinger inference
    /// pipeline: duration -> pitch -> variance -> acoustic -> vocoder.
    ///
    /// Stages are injected via setStages() as a StageSet (InferenceSpec +
    /// ImportOptions pairs extracted from a loaded singer package). run()
    /// executes the full pipeline on an InferenceRequest carrying
    /// pre-assembled SVS-level words, speakers, parameters and acoustic
    /// scalars, returning PCM audio in InferenceResult.
    ///
    /// \see 01-target-architecture.md section 3.2
    class DSINFER_EXPORT InferenceService {
    public:
        /// Backward-compatibility alias: InferenceService::StageSpec refers
        /// to the namespace-scope ds::infer::StageSpec.
        using StageSpec = ::ds::infer::StageSpec;

        InferenceService();
        ~InferenceService();

        /// Inject the 5 pipeline stages as a StageSet. Call before run().
        /// The spec pointers must remain valid until run() returns.
        /// Acoustic/vocoder sampleRate compatibility is validated here.
        srt::core::Expected<void> setStages(const StageSet &stages);

        /// Run the full 5-stage pipeline. Each stage creates an Inference,
        /// initializes it, starts it, and applies its output to the next
        /// stage's input. Returns PCM audio on success or a diagnostic on
        /// failure (No Hidden Fallback: the pipeline stops at the failing
        /// stage per v7 §6).
        InferenceResult run(const InferenceRequest &request);

    private:
        StageSet m_stages;
    };

} // namespace ds::infer
