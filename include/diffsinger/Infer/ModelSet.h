#pragma once

#include <filesystem>

#include <synthrt/Core/Support/Expected.h>
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
    /// Lite holds ModelSet and owns the final state storage.
    /// ModelSet does not include thread locking; the caller (lite) protects
    /// concurrency.
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
        /// returns the existing pointer. Default RuntimeOptions/InitArgs are
        /// constructed from spec->configuration().
        srt::core::Expected<srt::svs::Inference *> load(StageKind kind);

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
