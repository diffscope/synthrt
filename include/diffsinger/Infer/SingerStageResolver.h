#pragma once

#include <string>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/SVS/SingerContrib.h>

#include <diffsinger/Infer/InferenceService.h>
#include <diffsinger/Infer/StageKind.h>
#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

namespace ds::infer {

    /// StageSet — Named collection of the 5 inference stage specifications.
    /// Replaces array-by-index with self-documenting named fields.
    /// spec pointers remain valid as long as Runtime holds the package open.
    struct DSINFER_EXPORT StageSet {
        InferenceService::StageSpec duration;
        InferenceService::StageSpec pitch;
        InferenceService::StageSpec variance;
        InferenceService::StageSpec acoustic;
        InferenceService::StageSpec vocoder;

        /// Find a StageSpec by StageKind. Returns nullptr if not set.
        const InferenceService::StageSpec *find(StageKind kind) const noexcept;
    };

    /// SingerStageResolver resolves the 5 inference StageSpec entries
    /// from a singer's package imports into a StageSet.
    ///
    /// Returns a value-type StageSet that the caller can cache.
    /// The caller must ensure the package is loaded in Runtime (via
    /// Runtime::loadPackage()) before calling resolve().
    class DSINFER_EXPORT SingerStageResolver {
    public:
        SingerStageResolver() = default;
        ~SingerStageResolver() = default;

        /// Resolve from a loaded package (via Runtime).
        /// The package must already be loaded in the Runtime via runtime.loadPackage().
        /// Uses packageId + singerId + version for precise lookup.
        /// If \c version is empty, matches any version (backward compat).
        srt::core::Expected<StageSet> resolve(
            srt::core::Runtime &runtime,
            const std::string &packageId,
            const std::string &singerId,
            const std::string &version = {});

        /// Resolve directly from a SingerSpec pointer.
        /// Simpler overload when the caller already has the SingerSpec.
        srt::core::Expected<StageSet> resolve(
            const srt::svs::SingerSpec *singerSpec);
    };

} // namespace ds::infer
