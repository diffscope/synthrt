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

#include <filesystem>
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
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                                    "SingerStageResolver::resolve: singerSpec is null");
        }

        StageSet stageSet;

        // BUG-26: Track which stage slots have been filled to detect duplicate
        // imports of the same className. Per ROBUST-05, silent overwrite of
        // duplicate stage imports must be rejected explicitly. Mirrors the
        // SvsSingerAmbiguous rejection pattern in resolve(runtime,...) below
        // for multi-version singer ambiguity.
        bool slotFilled[std::size(kStageEntries)] = {};

        for (const auto &imp : singerSpec->imports()) {
            auto *inference = imp.inference();
            if (!inference) {
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::SvsStageResolveFailed,
                    "SingerStageResolver::resolve: unresolved inference import for singer: " +
                        singerSpec->id(),
                    singerSpec->id());
            }

            const auto &cls = inference->className();
            for (size_t i = 0; i < std::size(kStageEntries); ++i) {
                const auto &entry = kStageEntries[i];
                if (cls == entry.className) {
                    if (slotFilled[i]) {
                        return srt::core::Error::inferenceError(
                            srt::core::ErrorCode::SvsStageResolveFailed,
                            "SingerStageResolver::resolve: ambiguous import for singer " +
                                singerSpec->id() + ": className '" + cls +
                                "' is imported multiple times",
                            singerSpec->id(), cls);
                    }
                    slotFilled[i] = true;

                    if (!imp.options()) {
                        return srt::core::Error::inferenceError(
                            srt::core::ErrorCode::SvsStageResolveFailed,
                            "SingerStageResolver::resolve: import options missing for " + cls +
                                " in singer: " + singerSpec->id(),
                            singerSpec->id(), cls);
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
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::InferenceStageMissing,
                    std::string(kStageNames[i]) +
                        " inference not found for singer: " + singerSpec->id(),
                    singerSpec->id(), kStageNames[i]);
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
            return srt::core::Error(srt::core::ErrorCode::SessionError,
                                    "singer module category is not available");
        }
        auto *sc = singerCat->as<srt::svs::SingerCategory>();
        if (!sc) {
            return srt::core::Error(srt::core::ErrorCode::SessionError,
                                    "singer category cast failed");
        }

        // Collect singers matching the requested identity. A Runtime may hold
        // multiple packages/versions that each define a singer with the same id,
        // so packageId/version must be honored when supplied.
        std::vector<const srt::svs::SingerSpec *> candidates;
        stdc::VersionNumber requestedVersion;
        if (!version.empty()) {
            try {
                requestedVersion = stdc::VersionNumber::fromString(version);
            } catch (const std::exception &e) {
                // BUG-13: 第三方异常边界隔离（ROBUST-02）。fromString 是第三方接口，
                // 非法 version 字符串可能抛 std::exception，必须在此边界转换为 Error，
                // 不允许穿越 resolve 到上层（如 VoicebankSession::createModelSet）。
                return srt::core::Error(
                    srt::core::ErrorCode::InvalidArgument,
                    "SingerStageResolver::resolve: invalid version string '" + version +
                        "': " + e.what());
            }
        }
        for (const auto *singer : sc->singers()) {
            if (singer->id() != singerId) {
                continue;
            }
            if (!packageId.empty() && singer->packageId() != packageId) {
                continue;
            }
            if (!requestedVersion.isEmpty() && singer->packageVersion() != requestedVersion) {
                continue;
            }
            candidates.push_back(singer);
        }

        if (candidates.empty()) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::SvsSingerNotFound,
                "singer not found in loaded package: "
                "packageId=" + packageId +
                ", singerId=" + singerId +
                ", version=" + version,
                singerId)
                .withContext({}, {}, packageId)
                .withExtraContext({{"packageVersion", version}});
        }

        const srt::svs::SingerSpec *singerSpec = nullptr;
        if (candidates.size() == 1) {
            singerSpec = candidates.front();
        } else {
            // Multiple singers match the (packageId, singerId, version)
            // tuple — the caller must supply packageId/version to disambiguate.
            // Per D-32/K-06: multi-version/multi-package ambiguity must be
            // rejected explicitly, not silently resolved to one of them. Use
            // SvsSingerAmbiguous (mirrors G2pVersionAmbiguous on the G2P side)
            // rather than SvsSingerNotFound, since the singer WAS found — there
            // are just too many matches.
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::SvsSingerAmbiguous,
                "ambiguous singer: multiple singers with singerId=" + singerId +
                    " loaded for packageId=" + packageId + ", version=" +
                    version + "; provide packageId/version to disambiguate",
                singerId)
                .withContext({}, {}, packageId)
                .withExtraContext({{"packageVersion", version}});
        }

        return resolve(singerSpec);
    }

} // namespace ds::infer
