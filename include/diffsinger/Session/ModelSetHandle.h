#pragma once

#include <functional>
#include <memory>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>
#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/StageKind.h>
#include <diffsinger/Session/dssession_global.h>

namespace ds::session {

    class VoicebankSession;

    /// ModelSetHandle - A ModelSet bound to a specific VoicebankSnapshot
    /// generation. Returned by VoicebankSession::createModelSet().
    ///
    /// The handle keeps the underlying ModelSet alive and delegates load/stop/
    /// unload to it. start() is the only operation that rejects a stale handle
    /// with ErrorCode::StaleModelSet; running tasks may finish on a stale set
    /// (vnext 03 lifecycle contract).
    ///
    /// Staleness is determined by comparing the bound snapshot generation with
    /// the session's current generation. After a successful refresh the old
    /// handle becomes stale: start() returns StaleModelSet, but already-loaded
    /// models and already-running tasks are not aborted.
    class DSSESSION_EXPORT ModelSetHandle {
    public:
        ~ModelSetHandle();
        ModelSetHandle(const ModelSetHandle &) = delete;
        ModelSetHandle &operator=(const ModelSetHandle &) = delete;

        /// The snapshot generation this handle was created against.
        unsigned long long snapshotGeneration() const noexcept;

        /// Whether the bound snapshot has been replaced by a newer one.
        /// True after a successful refresh() that published a new snapshot.
        bool isStale() const noexcept;

        /// Lazily load the specified stage. Delegates to ModelSet::load().
        srt::core::Expected<srt::core::NO<srt::svs::Inference>> load(ds::infer::StageKind kind);

        /// Start a loaded stage. Rejects stale handles with
        /// ErrorCode::StaleModelSet. On success the result is retained and
        /// can be read repeatedly via result().
        srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
            start(ds::infer::StageKind kind, const srt::core::NO<srt::core::TaskStartInput> &input);

        /// Return the last result for a stage, or an empty NO when none exists.
        srt::core::NO<srt::core::TaskResult> result(ds::infer::StageKind kind) const;

        /// Stop and release one stage, clearing its retained result. A later
        /// start requires load() again.
        srt::core::Expected<void> reset(ds::infer::StageKind kind);

        /// Stop inference without releasing the model.
        srt::core::Expected<void> stop(ds::infer::StageKind kind);

        /// Unload the model (stop first, then release).
        srt::core::Expected<void> unload(ds::infer::StageKind kind);

        /// Unload all models in reverse pipeline order.
        srt::core::Expected<void> unloadAll();

        /// Query whether the specified stage is loaded.
        bool isLoaded(ds::infer::StageKind kind) const noexcept;

        /// Return the underlying StageSet (delegates to ModelSet::stages()).
        /// Lets hosts read per-stage InferenceImportOptions without re-resolving
        /// via SingerStageResolver. Stable for the handle's lifetime.
        const ds::infer::StageSet &stages() const noexcept;

    private:
        friend class VoicebankSession;
        /// Constructed only by VoicebankSession::createModelSet(). The
        /// isCurrentGenerationFn callback lets the handle query whether the
        /// session is still on the bound generation without holding a pointer
        /// to the session's private Impl. When the session is destroyed the
        /// callback returns false, making the handle report isStale()==true.
        ModelSetHandle(std::shared_ptr<ds::infer::ModelSet> modelSet,
                       unsigned long long generation,
                       std::function<bool()> isCurrentGenerationFn);
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace ds::session
