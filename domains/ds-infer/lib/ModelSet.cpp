// ModelSet.cpp - Per-stage lazy loading, reuse, stop, and unload of Inference.
//
// v2 Phase 3: Replaces SynthrtEngine's 5 NO<Inference> members with a unified
// lifecycle manager. Each stage is created + initialized on first load() call
// and reused thereafter. unload(kind) releases a single stage; unloadAll()
// releases in reverse pipeline order.

#include <diffsinger/Infer/ModelSet.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
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
        mutable std::mutex mutex;
        std::atomic_bool stale = false;
        StageSet stages;
        NO<srt::svs::Inference> duration;
        NO<srt::svs::Inference> pitch;
        NO<srt::svs::Inference> variance;
        NO<srt::svs::Inference> acoustic;
        NO<srt::svs::Inference> vocoder;
        NO<srt::core::TaskResult> durationResult;
        NO<srt::core::TaskResult> pitchResult;
        NO<srt::core::TaskResult> varianceResult;
        NO<srt::core::TaskResult> acousticResult;
        NO<srt::core::TaskResult> vocoderResult;
        bool durationStarted = false;
        bool pitchStarted = false;
        bool varianceStarted = false;
        bool acousticStarted = false;
        bool vocoderStarted = false;
        std::uint64_t durationEpoch = 0;
        std::uint64_t pitchEpoch = 0;
        std::uint64_t varianceEpoch = 0;
        std::uint64_t acousticEpoch = 0;
        std::uint64_t vocoderEpoch = 0;

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

        NO<srt::core::TaskResult> &resultSlot(StageKind kind) {
            return const_cast<NO<srt::core::TaskResult> &>(
                std::as_const(*this).resultSlot(kind));
        }

        const NO<srt::core::TaskResult> &resultSlot(StageKind kind) const {
            switch (kind) {
                case StageKind::Duration: return durationResult;
                case StageKind::Pitch:    return pitchResult;
                case StageKind::Variance: return varianceResult;
                case StageKind::Acoustic: return acousticResult;
                case StageKind::Vocoder:  return vocoderResult;
            }
            std::abort();
        }

        bool &startedSlot(StageKind kind) {
            switch (kind) {
                case StageKind::Duration: return durationStarted;
                case StageKind::Pitch:    return pitchStarted;
                case StageKind::Variance: return varianceStarted;
                case StageKind::Acoustic: return acousticStarted;
                case StageKind::Vocoder:  return vocoderStarted;
            }
            std::abort();
        }

        std::uint64_t &epochSlot(StageKind kind) {
            switch (kind) {
                case StageKind::Duration: return durationEpoch;
                case StageKind::Pitch:    return pitchEpoch;
                case StageKind::Variance: return varianceEpoch;
                case StageKind::Acoustic: return acousticEpoch;
                case StageKind::Vocoder:  return vocoderEpoch;
            }
            std::abort();
        }

        static srt::core::Error busyError(StageKind kind, const char *operation) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::ModelBusy,
                "ModelSet::" + std::string(operation) + ": " +
                    std::string(stageName(kind)) + " stage is busy",
                {}, stageName(kind));
        }

        static srt::core::Error staleError(StageKind kind, const char *operation) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::StaleModelSet,
                "ModelSet::" + std::string(operation) + ": model set is stale",
                {}, stageName(kind));
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
        std::unique_lock lock(_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return Impl::busyError(kind, "load");
        }
        if (_impl->stale.load(std::memory_order_acquire)) {
            return Impl::staleError(kind, "load");
        }

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

    srt::core::Expected<NO<srt::core::TaskResult>>
        ModelSet::start(StageKind kind, const NO<srt::core::TaskStartInput> &input) {
        NO<srt::svs::Inference> inference;
        std::uint64_t epoch = 0;
        {
            std::unique_lock lock(_impl->mutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                return Impl::busyError(kind, "start");
            }
            if (_impl->stale.load(std::memory_order_acquire)) {
                return Impl::staleError(kind, "start");
            }
            auto &slot = _impl->slot(kind);
            if (!slot) {
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::InferenceNotInitialized,
                    "ModelSet::start: " + std::string(stageName(kind)) + " stage is not loaded",
                    {}, stageName(kind));
            }
            if (_impl->startedSlot(kind) || slot->state() == srt::core::ITask::Running) {
                return Impl::busyError(kind, "start");
            }
            _impl->startedSlot(kind) = true;
            epoch = _impl->epochSlot(kind);
            inference = slot;
        }

        // Do not hold ModelSet's state mutex while executing the synchronous
        // plugin call: stop() must remain able to request cancellation.
        auto resultExp = inference->start(input);
        // A failed start still requires reset() before another attempt: the
        // plugin may have entered a terminal state with partially consumed input.
        if (!resultExp) {
            return std::move(resultExp.takeError());
        }
        {
            std::lock_guard lock(_impl->mutex);
            // reset()/unload() may have invalidated this run while it was
            // stopping. Do not resurrect an obsolete result.
            if (_impl->epochSlot(kind) == epoch) {
                _impl->resultSlot(kind) = resultExp.value();
            }
        }
        return resultExp.take();
    }

    NO<srt::core::TaskResult> ModelSet::result(StageKind kind) const {
        std::lock_guard lock(_impl->mutex);
        return _impl->resultSlot(kind);
    }

    srt::core::Expected<void> ModelSet::reset(StageKind kind) {
        std::unique_lock lock(_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return Impl::busyError(kind, "reset");
        }
        auto &slot = _impl->slot(kind);
        ++_impl->epochSlot(kind);
        if (slot && slot->state() == srt::core::ITask::Running && !slot->stop()) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceRunFailed,
                "ModelSet::reset: failed to stop " + std::string(stageName(kind)) + " inference",
                {}, stageName(kind));
        }
        slot.reset();
        _impl->resultSlot(kind).reset();
        _impl->startedSlot(kind) = false;
        return srt::core::Expected<void>();
    }

    void ModelSet::markStale() noexcept {
        _impl->stale.store(true, std::memory_order_release);
    }

    bool ModelSet::isStale() const noexcept {
        return _impl->stale.load(std::memory_order_acquire);
    }

    NO<srt::svs::Inference> &ModelSet::model(StageKind kind) noexcept {
        return _impl->slot(kind);
    }

    const NO<srt::svs::Inference> &ModelSet::model(StageKind kind) const noexcept {
        return _impl->slot(kind);
    }

    srt::core::Expected<void> ModelSet::stop(StageKind kind) {
        std::unique_lock lock(_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return Impl::busyError(kind, "stop");
        }
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
        std::unique_lock lock(_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return Impl::busyError(kind, "unload");
        }
        auto &slot = _impl->slot(kind);
        ++_impl->epochSlot(kind);
        if (!slot) {
            _impl->resultSlot(kind).reset();
            _impl->startedSlot(kind) = false;
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
        _impl->resultSlot(kind).reset();
        _impl->startedSlot(kind) = false;
        return srt::core::Expected<void>();
    }

    srt::core::Expected<void> ModelSet::unloadAll() {
        std::unique_lock lock(_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return Impl::busyError(StageKind::Vocoder, "unloadAll");
        }

        // Continue unloading remaining stages after a stop failure. Avoid
        // calling unload() here because it independently acquires the lock.
        srt::core::Error firstError;
        bool hadError = false;
        for (auto kind : kReverseOrder) {
            auto &slot = _impl->slot(kind);
            ++_impl->epochSlot(kind);
            if (!slot) {
                _impl->resultSlot(kind).reset();
                _impl->startedSlot(kind) = false;
                continue;
            }
            if (slot->state() == srt::core::ITask::Running && !slot->stop()) {
                if (!hadError) {
                    firstError = srt::core::Error::inferenceError(
                        srt::core::ErrorCode::InferenceRunFailed,
                        "ModelSet::unloadAll: failed to stop " +
                            std::string(stageName(kind)) + " inference",
                        {}, stageName(kind));
                    hadError = true;
                }
                continue;
            }
            slot.reset();
            _impl->resultSlot(kind).reset();
            _impl->startedSlot(kind) = false;
        }
        if (hadError) {
            return firstError;
        }
        return srt::core::Expected<void>();
    }

    bool ModelSet::isLoaded(StageKind kind) const noexcept {
        std::lock_guard lock(_impl->mutex);
        return static_cast<bool>(_impl->slot(kind));
    }

    const StageSet &ModelSet::stages() const noexcept {
        return _impl->stages;
    }

} // namespace ds::infer
