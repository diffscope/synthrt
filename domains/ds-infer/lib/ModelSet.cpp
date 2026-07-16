// ModelSet.cpp - Per-stage lazy loading, reuse, stop, and unload of Inference.
//
// v2 Phase 3: Replaces SynthrtEngine's 5 NO<Inference> members with a unified
// lifecycle manager. Each stage is created + initialized on first load() call
// and reused thereafter. unload(kind) releases a single stage; unloadAll()
// releases in reverse pipeline order.

#include <diffsinger/Infer/ModelSet.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/SVS/InferenceContrib.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds::infer {

    namespace Ac = srt::svs::Api::Acoustic::L1;
    namespace Dur = srt::svs::Api::Duration::L1;
    namespace Pit = srt::svs::Api::Pitch::L1;
    namespace Var = srt::svs::Api::Variance::L1;
    namespace Vo = srt::svs::Api::Vocoder::L1;

    using srt::core::NO;

    // Reverse pipeline order for unloadAll().
    static const StageKind kReverseOrder[] = {
        StageKind::Vocoder, StageKind::Acoustic, StageKind::Variance,
        StageKind::Pitch, StageKind::Duration,
    };

    static const char *stageName(StageKind kind) {
        switch (kind) {
            case StageKind::Duration: return "duration";
            case StageKind::Pitch:    return "pitch";
            case StageKind::Variance: return "variance";
            case StageKind::Acoustic: return "acoustic";
            case StageKind::Vocoder:  return "vocoder";
        }
        return "unknown";
    }

    // Helper: create + initialize an Inference from a StageSpec.
    // Uses the same pattern as InferenceService::createAndInit.
    template <class InitArgs, class RuntimeOptions>
    static srt::core::Expected<NO<srt::svs::Inference>>
        createAndInit(const InferenceService::StageSpec &stage) {
        NO<srt::svs::Inference> inference;
        auto createExp = stage.spec->createInference(stage.options,
                                                      NO<RuntimeOptions>::create());
        if (!createExp) {
            // B-07: propagate the inner Error (preserving trace/code) and
            // append the current layer instead of constructing a fresh Error
            // from only the message string.
            return std::move(createExp.takeError()
                .withTrace(std::source_location::current(),
                           "ModelSet::createAndInit(" + std::string(stageName(stage.kind)) + ")")
                .withContext({}, stageName(stage.kind)));
        }
        inference = createExp.take();

        auto initExp = inference->initialize(NO<InitArgs>::create());
        if (!initExp) {
            return std::move(initExp.takeError()
                .withTrace(std::source_location::current(),
                           "ModelSet::createAndInit(" + std::string(stageName(stage.kind)) + ")")
                .withContext({}, stageName(stage.kind)));
        }
        return inference;
    }

    class ModelSet::Impl {
    public:
        StageSet stages;
        NO<srt::svs::Inference> duration;
        NO<srt::svs::Inference> pitch;
        NO<srt::svs::Inference> variance;
        NO<srt::svs::Inference> acoustic;
        NO<srt::svs::Inference> vocoder;

        explicit Impl(StageSet s) : stages(std::move(s)) {}

        NO<srt::svs::Inference> &slot(StageKind kind) {
            switch (kind) {
                case StageKind::Duration: return duration;
                case StageKind::Pitch:    return pitch;
                case StageKind::Variance: return variance;
                case StageKind::Acoustic: return acoustic;
                case StageKind::Vocoder:  return vocoder;
            }
            // Unreachable for valid StageKind values.
            std::abort();
        }

        const InferenceService::StageSpec &stageSpec(StageKind kind) const {
            switch (kind) {
                case StageKind::Duration: return stages.duration;
                case StageKind::Pitch:    return stages.pitch;
                case StageKind::Variance: return stages.variance;
                case StageKind::Acoustic: return stages.acoustic;
                case StageKind::Vocoder:  return stages.vocoder;
            }
            std::abort();
        }

        static srt::core::Expected<NO<srt::svs::Inference>>
            createForKind(StageKind kind, const InferenceService::StageSpec &stage) {
            switch (kind) {
                case StageKind::Duration:
                    return createAndInit<Dur::DurationInitArgs, Dur::DurationRuntimeOptions>(stage);
                case StageKind::Pitch:
                    return createAndInit<Pit::PitchInitArgs, Pit::PitchRuntimeOptions>(stage);
                case StageKind::Variance:
                    return createAndInit<Var::VarianceInitArgs, Var::VarianceRuntimeOptions>(stage);
                case StageKind::Acoustic:
                    return createAndInit<Ac::AcousticInitArgs, Ac::AcousticRuntimeOptions>(stage);
                case StageKind::Vocoder:
                    return createAndInit<Vo::VocoderInitArgs, Vo::VocoderRuntimeOptions>(stage);
            }
            return std::move(srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceInputInvalid,
                "ModelSet: unknown StageKind")
                .withTrace(std::source_location::current(), "ModelSet::createForKind"));
        }
    };

    ModelSet::ModelSet(StageSet stages)
        : _impl(std::make_unique<Impl>(std::move(stages))) {}

    ModelSet::~ModelSet() = default;

    ModelSet::ModelSet(ModelSet &&) noexcept = default;
    ModelSet &ModelSet::operator=(ModelSet &&) noexcept = default;

    srt::core::Expected<NO<srt::svs::Inference>> ModelSet::load(StageKind kind) {
        auto &slot = _impl->slot(kind);
        if (slot) {
            return slot;
        }

        const auto &spec = _impl->stageSpec(kind);
        if (!spec.spec) {
            return std::move(srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceInputInvalid,
                "ModelSet::load: " + std::string(stageName(kind)) +
                    " stage spec is null",
                {}, stageName(kind))
                .withTrace(std::source_location::current(),
                           "ModelSet::load(" + std::string(stageName(kind)) + ")"));
        }

        auto infExp = Impl::createForKind(kind, spec);
        if (!infExp) {
            return std::move(infExp.takeError()
                .withTrace(std::source_location::current(),
                           "ModelSet::load(" + std::string(stageName(kind)) + ")")
                .withContext({}, stageName(kind)));
        }
        slot = infExp.take();
        return slot;
    }

    NO<srt::svs::Inference> &ModelSet::model(StageKind kind) noexcept {
        return _impl->slot(kind);
    }

    const NO<srt::svs::Inference> &ModelSet::model(StageKind kind) const noexcept {
        return _impl->slot(kind);
    }

    srt::core::Expected<void> ModelSet::stop(StageKind kind) {
        auto &slot = _impl->slot(kind);
        if (!slot) {
            return srt::core::Expected<void>();
        }
        // BF-24: If the model is not Running (e.g. already Idle/Terminated/Failed),
        // stop() returning false is normal and must not be reported as an error.
        // Only report an error if the model was Running and stop() failed.
        if (slot->state() != srt::core::ITask::Running) {
            return srt::core::Expected<void>();
        }
        if (!slot->stop()) {
            return std::move(srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceRunFailed,
                "ModelSet::stop: failed to stop " + std::string(stageName(kind)) +
                    " inference",
                {}, stageName(kind))
                .withTrace(std::source_location::current(),
                           "ModelSet::stop(" + std::string(stageName(kind)) + ")"));
        }
        return srt::core::Expected<void>();
    }

    srt::core::Expected<void> ModelSet::unload(StageKind kind) {
        auto &slot = _impl->slot(kind);
        if (!slot) {
            return srt::core::Expected<void>();
        }
        // Stop first, then release.
        if (slot->state() == srt::core::ITask::Running) {
            if (!slot->stop()) {
                return std::move(srt::core::Error::inferenceError(
                    srt::core::ErrorCode::InferenceRunFailed,
                    "ModelSet::unload: failed to stop " + std::string(stageName(kind)) +
                        " inference",
                    {}, stageName(kind))
                    .withTrace(std::source_location::current(),
                               "ModelSet::unload(" + std::string(stageName(kind)) + ")"));
            }
        }
        slot.reset();
        return srt::core::Expected<void>();
    }

    srt::core::Expected<void> ModelSet::unloadAll() {
        // Continue unloading all remaining stages even if one fails, so that
        // a single stop() failure does not leak the other stages. Return the
        // first error encountered.
        srt::core::Error firstError;
        bool hadError = false;
        for (auto kind : kReverseOrder) {
            auto exp = unload(kind);
            if (!exp && !hadError) {
                firstError = exp.error();
                hadError = true;
            }
        }
        if (hadError) {
            return firstError;
        }
        return srt::core::Expected<void>();
    }

    bool ModelSet::isLoaded(StageKind kind) const noexcept {
        return static_cast<bool>(_impl->slot(kind));
    }

    const StageSet &ModelSet::stages() const noexcept {
        return _impl->stages;
    }

} // namespace ds::infer
