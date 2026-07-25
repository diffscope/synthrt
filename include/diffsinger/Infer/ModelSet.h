#pragma once

#include <filesystem>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/SVS/InferenceContrib.h>

#include <diffsinger/Infer/InferenceService.h>
#include <diffsinger/Infer/StageKind.h>
#include <diffsinger/Infer/SingerStageResolver.h>
#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

namespace ds::infer {

    /// ModelSet - Per-stage lazy-loading, reuse, stop, and unload of Inference
    /// objects. Constructed from a StageSet produced by
    /// SingerStageResolver::resolve().
    ///
    /// Operations are synchronized and non-reentrant: a concurrent operation
    /// returns ErrorCode::ModelBusy. load() and start() reject a stale set with
    /// ErrorCode::StaleModelSet. Existing model() remains a compatibility escape
    /// hatch; callers using it must synchronize direct Inference access.

    ///
    /// \see docs/refactoring-v2/02-target-api.md section 6
    class DSINFER_EXPORT ModelSet {
    public:
        explicit ModelSet(StageSet stages);
        ~ModelSet();

        ModelSet(const ModelSet &) = delete;
        ModelSet &operator=(const ModelSet &) = delete;
        ModelSet(ModelSet &&) noexcept;
        ModelSet &operator=(ModelSet &&) noexcept;

        /// Lazily load the specified stage. If the model is already loaded,
        /// returns the existing pointer. Rejects stale sets.
        srt::core::Expected<srt::core::NO<srt::svs::Inference>> load(StageKind kind);

        /// Start a loaded stage. A successful task result is retained and can
        /// be read repeatedly via result(). Failed or stopped stages require
        /// reset() before another start(). Rejects stale sets.
        srt::core::Expected<srt::core::NO<srt::core::TaskResult>> start(
            StageKind kind, const srt::core::NO<srt::core::TaskStartInput> &input);

        /// Return the last result for a stage, or an empty NO when none exists.
        srt::core::NO<srt::core::TaskResult> result(StageKind kind) const;

        /// Stop and release one stage, clearing its retained result. This is
        /// the required transition after a failed/cancelled task before retry.
        srt::core::Expected<void> reset(StageKind kind);

        /// Mark this set obsolete after its bound resource snapshot changes.
        /// Running work may finish; future load()/start() calls are rejected.
        void markStale() noexcept;
        bool isStale() const noexcept;

        /// Clear the stale flag set by \c markStale(), allowing the ModelSet
        /// instance to be reused for subsequent \c load()/start() calls.
        ///
        /// This is an opt-in escape hatch for callers that cannot easily
        /// rebuild a fresh ModelSet instance (e.g. long-lived session shells
        /// that need to preserve their StageSet binding across reloads).
        ///
        /// \par Contract (ROBUST-04)
        /// - The caller MUST ensure no \c start() is in flight on any stage
        ///   before calling \c clearStale(). Use \c stop() / \c unload() to
        ///   drain running work first.
        /// - \c clearStale() does NOT release already-loaded Inference
        ///   objects, reset per-stage \c epochSlot values, or clear retained
        ///   TaskResult entries. After \c clearStale(), the caller MUST
        ///   \c unload() (or \c unloadAll()) any stage whose underlying
        ///   spec / package may have changed before re-\c load()ing it, so
        ///   that the next \c load() re-creates the Inference from the
        ///   (potentially updated) spec snapshot.
        /// - \c clearStale() does NOT auto-rebuild; the StageSet stored at
        ///   construction is unchanged.
        ///
        /// \note The default lifecycle pattern (per ARCH-05) remains: when a
        ///       package is reloaded, mark the old ModelSet stale and build a
        ///       new ModelSet from the fresh StageSet. \c clearStale() is
        ///       provided for callers that explicitly opt into instance reuse
        ///       and accept the additional bookkeeping responsibility.
        void clearStale() noexcept;

        /// Get the loaded model NO reference. Returns an empty NO if not loaded.
        srt::core::NO<srt::svs::Inference> &model(StageKind kind) noexcept;
        const srt::core::NO<srt::svs::Inference> &model(StageKind kind) const noexcept;

        /// Stop inference without releasing the model.
        srt::core::Expected<void> stop(StageKind kind);

        /// Unload the model (stop first, then release).
        srt::core::Expected<void> unload(StageKind kind);

        /// Unload all models in reverse order (vocoder -> acoustic -> variance
        /// -> pitch -> duration).
        srt::core::Expected<void> unloadAll();

        /// Query whether the specified stage is loaded.
        bool isLoaded(StageKind kind) const noexcept;

        /// Return the StageSet passed at construction.
        const StageSet &stages() const noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace ds::infer
