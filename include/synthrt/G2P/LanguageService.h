#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/LanguageRoute.h>
#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/S2P/LanguageResource.h>

namespace srt::g2p {

    /// LanguageService — G2P initialization + route resolution + batch conversion,
    /// plus S2P/Onset discovery.
    ///
    /// G2P Manager is a process-level singleton (initialized by LanguageService).
    /// LanguageService instances are lightweight handles, safe to use across
    /// multiple threads after initialize().
    ///
    /// Two-stage loading (MGR):
    ///   - Stage 1 (initializeMetadata): registers plugin paths, official G2P
    ///     packages, and voicebank G2P contexts. After this call,
    ///     resolveLanguageRoute() / resolveS2pResource() are ready. Does NOT
    ///     load ONNX models.
    ///   - Stage 2 (initializeModels): loads G2P plugin DLLs, creates ONNX
    ///     sessions, and calls Manager::initialize(). After this call,
    ///     convertLyric() / convert() are ready. On failure, G2P conversion is
    ///     disabled but route resolution remains available.
    ///   - initialize() is a convenience wrapper that calls both stages.
    class SRT_G2P_EXPORT LanguageService {
    public:
        LanguageService();
        ~LanguageService();

        // === Stage 1: Metadata initialization (fast, no ONNX) ===
        //
        // Registers G2P plugin paths, official G2P packages, and per-singer
        // voicebank G2P packages. The packageDirs map is the output of
        // VoicebankScanner::packageDirectory().
        //
        // After this call, resolveLanguageRoute() and resolveS2pResource()
        // are ready (route resolution only needs manifest metadata, not ONNX
        // models). Sets metadataReady() = true on success.
        srt::core::Expected<void> initializeMetadata(
            const std::vector<std::filesystem::path> &pluginSearchPaths,
            const std::vector<std::filesystem::path> &officialG2pPackagePaths,
            const std::unordered_map<std::string, std::filesystem::path> &packageDirs);

        // === Stage 2: Model initialization (slow, loads ONNX) ===
        //
        // Loads G2P plugin DLLs, creates ONNX sessions, and initializes the
        // Manager. On failure, returns an error and the caller should disable
        // G2P conversion (but route resolution remains available). Requires
        // initializeMetadata() to have been called first. Sets modelsReady() =
        // true on success.
        srt::core::Expected<void> initializeModels();

        // === Convenience: initialize = Stage 1 + Stage 2 ===
        //
        // Backward-compatible wrapper that calls initializeMetadata() followed
        // by initializeModels(). Equivalent to the legacy initialize() flow.
        srt::core::Expected<void> initialize(
            const std::vector<std::filesystem::path> &pluginSearchPaths,
            const std::vector<std::filesystem::path> &officialG2pPackagePaths,
            const std::unordered_map<std::string, std::filesystem::path> &packageDirs);

        // === Readiness queries ===
        bool metadataReady() const;  ///< Stage 1 completed
        bool modelsReady() const;    ///< Stage 2 completed
        bool ready() const;          ///< metadataReady() && modelsReady()

        // === Per-singer route ===
        //
        // Resolves G2P route + S2P resource + onset for a singer+language.
        // Requires metadataReady() (route resolution uses manifest metadata
        // only, no ONNX models).
        srt::core::Expected<LanguageRoute> resolveLanguageRoute(
            const std::string &packageId,
            const std::string &singerId,
            const std::string &languageId) const;

        // === Per-singer S2P resource ===
        //
        // Resolves and caches the S2P LanguageResource for a singer+language.
        // Returns a shared_ptr so the host can call convert() directly.
        // The resource is cached per (packageId, singerId, languageId) tuple;
        // subsequent calls with the same key return the cached resource.
        srt::core::Expected<std::shared_ptr<srt::s2p::LanguageResource>>
        resolveS2pResource(const std::string &packageId,
                           const std::string &singerId,
                           const std::string &languageId) const;

        // === Batch G2P conversion ===
        //
        // Requires modelsReady() (conversion needs the ONNX runtime).
        std::vector<srt::g2p::G2pRes> convertLyric(
            const std::vector<srt::g2p::G2pInput> &input) const;

        // === Convenience: route resolution + batch conversion ===
        //
        // Resolves the route for (packageId, singerId, languageId) and runs
        // convertLyric() on \p inputs. On route resolution failure returns an
        // Expected error; on success returns the G2P result vector (individual
        // results may still carry per-lyric errors via G2pRes::isFailed()).
        // Requires modelsReady().
        srt::core::Expected<std::vector<srt::g2p::G2pRes>> convert(
            const std::string &packageId,
            const std::string &singerId,
            const std::string &languageId,
            const std::vector<srt::g2p::G2pInput> &inputs) const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace srt::g2p
