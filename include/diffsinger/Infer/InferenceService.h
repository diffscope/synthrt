#pragma once

#include <filesystem>

#include <diffsinger/Infer/InferenceRequest.h>
#include <diffsinger/Infer/InferenceResult.h>
#include <diffsinger/Infer/StageKind.h>
#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include <synthrt/SVS/InferenceContrib.h>

namespace ds::infer {

    /// InferenceService - Orchestrates the 5-stage DiffSinger inference
    /// pipeline: duration -> pitch -> variance -> acoustic -> vocoder.
    ///
    /// Stages are injected via setStages() as InferenceSpec + ImportOptions
    /// pairs extracted from a loaded singer package. run() executes the full
    /// pipeline on an InferenceRequest carrying pre-assembled SVS-level words,
    /// speakers, parameters and acoustic scalars, returning PCM audio in
    /// InferenceResult.
    ///
    /// \see 01-target-architecture.md section 3.2
    class DSINFER_EXPORT InferenceService {
    public:
        /// A single pipeline stage: an InferenceSpec (from a loaded singer
        /// import) plus its ImportOptions. The spec is not owned by this
        /// struct; the caller must keep it alive for the service's lifetime.
        struct StageSpec {
            StageKind kind = StageKind::Duration;           ///< v2: stage identifier
            srt::svs::InferenceSpec *spec = nullptr;
            srt::core::NO<srt::svs::InferenceImportOptions> options;
            std::filesystem::path packageDirectory;         ///< v2: source package directory
        };

        InferenceService();
        ~InferenceService();

        /// Inject the 5 pipeline stages. Call before run(). The spec pointers
        /// must remain valid until run() returns. Acoustic/vocoder sampleRate
        /// compatibility is validated here.
        srt::core::Expected<void> setStages(const StageSpec &duration,
                                            const StageSpec &pitch,
                                            const StageSpec &variance,
                                            const StageSpec &acoustic,
                                            const StageSpec &vocoder);

        /// Run the full 5-stage pipeline. Each stage creates an Inference,
        /// initializes it, starts it, and applies its output to the next
        /// stage's input. Returns PCM audio on success or a diagnostic on
        /// failure (No Hidden Fallback: the pipeline stops at the failing
        /// stage per v7 §6).
        InferenceResult run(const InferenceRequest &request);

        // === Mode B: Per-stage (NEW) ===
        //
        // Each method creates a temporary Inference from the cached StageSpec,
        // runs it, returns the result. Inference does not persist between
        // calls. Call setStages() first.
        //
        // If the caller wants persistent Inference objects across stages,
        // they should create Inference directly from StageSpec and call
        // start()/stop() themselves (see 06-caller-contract.md section 2.3).

        srt::core::Expected<srt::core::NO<srt::svs::Api::Duration::L1::DurationResult>>
            runDuration(const srt::core::NO<srt::svs::Api::Duration::L1::DurationStartInput> &input);
        srt::core::Expected<srt::core::NO<srt::svs::Api::Pitch::L1::PitchResult>>
            runPitch(const srt::core::NO<srt::svs::Api::Pitch::L1::PitchStartInput> &input);
        srt::core::Expected<srt::core::NO<srt::svs::Api::Variance::L1::VarianceResult>>
            runVariance(const srt::core::NO<srt::svs::Api::Variance::L1::VarianceStartInput> &input);
        srt::core::Expected<srt::core::NO<srt::svs::Api::Acoustic::L1::AcousticResult>>
            runAcoustic(const srt::core::NO<srt::svs::Api::Acoustic::L1::AcousticStartInput> &input);
        srt::core::Expected<srt::core::NO<srt::svs::Api::Vocoder::L1::VocoderResult>>
            runVocoder(const srt::core::NO<srt::svs::Api::Vocoder::L1::VocoderStartInput> &input);

    private:
        StageSpec m_duration;
        StageSpec m_pitch;
        StageSpec m_variance;
        StageSpec m_acoustic;
        StageSpec m_vocoder;
    };

} // namespace ds::infer
