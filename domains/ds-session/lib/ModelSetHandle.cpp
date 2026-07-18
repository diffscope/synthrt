// ModelSetHandle.cpp - Snapshot-bound ModelSet lifecycle handle.
//
// WP4: Wraps a ds::infer::ModelSet together with the VoicebankSnapshot
// generation it was created against. start() rejects a stale handle with
// ErrorCode::StaleModelSet so the host can rebuild against the newer
// snapshot; load/stop/unload remain usable so already-running tasks may
// finish and inspect their results.
//
// The handle keeps snapshot-generation staleness separate from ModelSet's own
// lifecycle state. It delegates stage execution to ModelSet so start/stop/reset
// share the same non-reentrant synchronization boundary.

#include <diffsinger/Session/ModelSetHandle.h>

#include <map>
#include <mutex>
#include <utility>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/SVS/Inference.h>

#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/StageKind.h>

namespace ds::session {

    using srt::core::NO;
    using srt::core::Expected;
    using srt::core::Error;
    using srt::core::ErrorCode;
    using srt::core::TaskResult;
    using srt::core::TaskStartInput;
    using ds::infer::ModelSet;
    using ds::infer::StageKind;

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

    class ModelSetHandle::Impl {
    public:
        std::shared_ptr<ModelSet> modelSet;
        unsigned long long generation = 0;
        std::function<bool()> isCurrentGenerationFn;

        Impl(std::shared_ptr<ModelSet> ms,
             unsigned long long gen,
             std::function<bool()> fn)
            : modelSet(std::move(ms)),
              generation(gen),
              isCurrentGenerationFn(std::move(fn)) {}

        Error staleError(const char *operation, StageKind kind) const {
            return Error::inferenceError(
                ErrorCode::StaleModelSet,
                "ModelSetHandle::" + std::string(operation) + ": " +
                    std::string(stageName(kind)) +
                    " stage handle is stale (snapshot replaced)",
                {}, stageName(kind));
        }
    };

    ModelSetHandle::ModelSetHandle(std::shared_ptr<ModelSet> modelSet,
                                   unsigned long long generation,
                                   std::function<bool()> isCurrentGenerationFn)
        : _impl(std::make_unique<Impl>(std::move(modelSet), generation,
                                       std::move(isCurrentGenerationFn))) {}

    ModelSetHandle::~ModelSetHandle() = default;

    unsigned long long ModelSetHandle::snapshotGeneration() const noexcept {
        return _impl->generation;
    }

    bool ModelSetHandle::isStale() const noexcept {
        // When the session is destroyed the callback returns false (the
        // weak_ptr to the session Impl expires), so the handle reports stale.
        // This is intentional: a handle outliving its session cannot start
        // new work.
        return !_impl->isCurrentGenerationFn();
    }

    Expected<NO<srt::svs::Inference>> ModelSetHandle::load(StageKind kind) {
        // load() intentionally does NOT reject stale handles: a caller may
        // inspect or finish work on an already-loaded model after the
        // snapshot has moved on. Only start() rejects staleness.
        return _impl->modelSet->load(kind);
    }

    Expected<NO<TaskResult>>
        ModelSetHandle::start(StageKind kind, const NO<TaskStartInput> &input) {
        // Staleness check: the only operation that rejects a stale handle.
        // Already-running tasks are not aborted; they may finish on the
        // stale set (vnext 03 lifecycle contract).
        if (isStale()) {
            return _impl->staleError("start", kind);
        }
        return _impl->modelSet->start(kind, input);
    }

    NO<TaskResult> ModelSetHandle::result(StageKind kind) const {
        return _impl->modelSet->result(kind);
    }

    Expected<void> ModelSetHandle::reset(StageKind kind) {
        return _impl->modelSet->reset(kind);
    }

    Expected<void> ModelSetHandle::stop(StageKind kind) {
        // Stop inference without releasing the model or clearing the result.
        return _impl->modelSet->stop(kind);
    }

    Expected<void> ModelSetHandle::unload(StageKind kind) {
        return _impl->modelSet->unload(kind);
    }

    Expected<void> ModelSetHandle::unloadAll() {
        return _impl->modelSet->unloadAll();
    }

    bool ModelSetHandle::isLoaded(StageKind kind) const noexcept {
        return _impl->modelSet->isLoaded(kind);
    }

    const ds::infer::StageSet &ModelSetHandle::stages() const noexcept {
        return _impl->modelSet->stages();
    }

} // namespace ds::session
