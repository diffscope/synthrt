#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/S2P/LanguageResource.h>

#include <diffsinger/Lang/LanguageRoute.h>

namespace ds::lang {

    /// LanguageService — G2P initialization + route resolution + batch conversion,
    /// plus S2P/Onset discovery.
    ///
    /// G2P Manager is a process-level singleton (initialized by LanguageService).
    /// LanguageService instances are lightweight handles, safe to use across
    /// multiple threads after initialize().
    class SRT_G2P_EXPORT LanguageService {
    public:
        LanguageService();
        ~LanguageService();

        // === Initialize (call once) ===
        //
        // Registers G2P plugin paths, official G2P packages, and per-singer
        // voicebank G2P packages. The packageDirs map is the output of
        // VoicebankScanner::packageDirectory().
        //
        // After initialize(), convertLyric() is ready.
        srt::core::Expected<void> initialize(
            const std::vector<std::filesystem::path> &pluginSearchPaths,
            const std::vector<std::filesystem::path> &officialG2pPackagePaths,
            const std::unordered_map<std::string, std::filesystem::path> &packageDirs);

        // === Per-singer route ===
        //
        // Resolves G2P route + S2P resource + onset for a singer+language.
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
        std::vector<srt::g2p::G2pRes> convertLyric(
            const std::vector<srt::g2p::G2pInput> &input) const;

        // === Convenience: bool convert (optional) ===
        // Batch lyric -> phoneme conversion with route resolution in one call.
        // Returns true if all conversions succeeded. When \a error is non-null,
        // failure details are written to it (route resolution error or the
        // first failed G2P result); otherwise failures are silent.
        bool convert(const std::string &packageId,
                     const std::string &singerId,
                     const std::string &languageId,
                     const std::vector<srt::g2p::G2pInput> &inputs,
                     std::vector<srt::g2p::G2pRes> &outputs,
                     srt::core::Diagnostic *error = nullptr) const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace ds::lang
