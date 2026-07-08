// SingerStageResolver.cpp - Resolve 5 inference stages from a singer's imports.
//
// Extracted from DiffSingerSession::runInference() (L356-L431) as part of
// v1 Phase 3 P0-a. The resolver finds the SingerSpec by singerId in a loaded
// Runtime, then maps its imports' className to the 5 known DiffSinger
// inference stages (duration/pitch/variance/acoustic/vocoder).
//
// v2 Phase 4: resolve() now accepts a version parameter for precise lookup.
// StageSpec entries are populated with kind + packageDirectory.

#include <diffsinger/Infer/SingerStageResolver.h>

#include <string>
#include <vector>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/SVS/SingerContrib.h>
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

    // Known DiffSinger inference className -> stage slot + kind mapping.
    // These 5 classNames are the DiffSinger convention (see dsinfer-cli
    // main.cpp and DiffSingerSession::runInference).
    struct StageEntry {
        const char *className;
        StageKind kind;
        InferenceService::StageSpec StageSet::*slot;
    };

    static const StageEntry kStageEntries[] = {
        {Dur::API_CLASS, StageKind::Duration, &StageSet::duration},
        {Pit::API_CLASS, StageKind::Pitch,    &StageSet::pitch   },
        {Var::API_CLASS, StageKind::Variance, &StageSet::variance},
        {Ac::API_CLASS,  StageKind::Acoustic, &StageSet::acoustic},
        {Vo::API_CLASS,  StageKind::Vocoder,  &StageSet::vocoder },
    };

    const InferenceService::StageSpec *
        StageSet::find(StageKind kind) const noexcept {
        switch (kind) {
            case StageKind::Duration: return &duration;
            case StageKind::Pitch:    return &pitch;
            case StageKind::Variance: return &variance;
            case StageKind::Acoustic: return &acoustic;
            case StageKind::Vocoder:  return &vocoder;
        }
        return nullptr;
    }

    srt::core::Expected<StageSet>
        SingerStageResolver::resolve(const srt::svs::SingerSpec *singerSpec) {
        if (!singerSpec) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                                    "SingerStageResolver::resolve: singerSpec is null");
        }

        StageSet stageSet;

        for (const auto &imp : singerSpec->imports()) {
            auto *inference = imp.inference();
            if (!inference) {
                return srt::core::Error(
                    srt::core::Error::FileNotFound,
                    "SingerStageResolver::resolve: unresolved inference import for singer: " +
                        singerSpec->id());
            }

            const auto &cls = inference->className();
            for (const auto &entry : kStageEntries) {
                if (cls == entry.className) {
                    if (!imp.options()) {
                        return srt::core::Error(
                            srt::core::Error::InvalidFormat,
                            "SingerStageResolver::resolve: import options missing for " + cls +
                                " in singer: " + singerSpec->id());
                    }

                    (stageSet.*(entry.slot)).kind = entry.kind;
                    (stageSet.*(entry.slot)).spec = inference;
                    (stageSet.*(entry.slot)).options = imp.options();
                    (stageSet.*(entry.slot)).packageDirectory = inference->path();
                    break;
                }
            }
        }

        // Validate all 5 stages are present.
        static const char *const kStageNames[] = {
            "duration", "pitch", "variance", "acoustic", "vocoder"};
        const InferenceService::StageSpec *stages[] = {
            &stageSet.duration, &stageSet.pitch, &stageSet.variance,
            &stageSet.acoustic, &stageSet.vocoder};
        for (size_t i = 0; i < std::size(kStageEntries); ++i) {
            if (!stages[i]->spec) {
                return srt::core::Error(
                    srt::core::Error::FileNotFound,
                    std::string(kStageNames[i]) +
                        " inference not found for singer: " + singerSpec->id());
            }
        }

        return stageSet;
    }

    srt::core::Expected<StageSet>
        SingerStageResolver::resolve(srt::core::Runtime &runtime,
                                     const std::string &packageId,
                                     const std::string &singerId,
                                     const std::string &version) {
        auto *singerCat = runtime.moduleCategory("singer");
        if (!singerCat) {
            return srt::core::Error(srt::core::Error::SessionError,
                                    "singer module category is not available");
        }
        auto *sc = singerCat->as<srt::svs::SingerCategory>();
        if (!sc) {
            return srt::core::Error(srt::core::Error::SessionError,
                                    "singer category cast failed");
        }

        // Collect all singers matching singerId. A Runtime may hold multiple
        // packages/versions that each define a singer with the same id.
        std::vector<const srt::svs::SingerSpec *> candidates;
        for (const auto *singer : sc->singers()) {
            if (singer->id() == singerId) {
                candidates.push_back(singer);
            }
        }

        if (candidates.empty()) {
            return srt::core::Error(srt::core::Error::FileNotFound,
                                    "singer not found in loaded package: "
                                    "packageId=" + packageId +
                                    ", singerId=" + singerId +
                                    ", version=" + version);
        }

        const srt::svs::SingerSpec *singerSpec = nullptr;
        if (candidates.size() == 1) {
            // Backward-compatible: a single match is used directly, ignoring
            // packageId/version (which are empty in the common CLI scenario).
            singerSpec = candidates.front();
        } else if (!packageId.empty() || !version.empty()) {
            // Multiple candidates with the same singerId. SingerSpec does not
            // expose packageId/version directly, so attempt a best-effort
            // disambiguation: the spec's path() contains the package
            // directory, which usually embeds the packageId. version cannot
            // be derived from the path and is only used to decide whether
            // disambiguation is required.
            for (const auto *cand : candidates) {
                const auto pathStr = cand->path().string();
                if (!packageId.empty() &&
                    pathStr.find(packageId) == std::string::npos) {
                    continue;
                }
                if (singerSpec) {
                    // Still ambiguous: more than one candidate matched.
                    singerSpec = nullptr;
                    break;
                }
                singerSpec = cand;
            }
            if (!singerSpec) {
                return srt::core::Error(
                    srt::core::Error::FileNotFound,
                    "ambiguous singer: multiple singers with singerId=" +
                        singerId + " loaded; cannot disambiguate by "
                        "packageId/version (packageId=" + packageId +
                        ", version=" + version + ")");
            }
        } else {
            // Multiple candidates but no packageId/version supplied to
            // disambiguate. Preserve legacy behavior (first match) would be
            // silently incorrect, so report the ambiguity instead.
            return srt::core::Error(
                srt::core::Error::FileNotFound,
                "ambiguous singer: multiple singers with singerId=" + singerId +
                    " loaded; provide packageId/version to disambiguate");
        }

        return resolve(singerSpec);
    }

} // namespace ds::infer
