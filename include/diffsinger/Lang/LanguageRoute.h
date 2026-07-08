#pragma once

#include <filesystem>
#include <string>

#include <stdcorelib/support/versionnumber.h>

namespace ds::lang {

    /// LanguageRoute - G2P route + S2P resource, returned by
    /// LanguageService::resolveLanguageRoute().
    ///
    /// Combines G2P routing info and S2P resource references so the host
    /// (CLI/lite) doesn't need to call lower-level resolve methods.
    struct LanguageRoute {
        std::string g2pId;                 ///< Actual G2P plugin task id
        std::string singerId;              ///< context = singerId (voicebank private G2P)
        stdc::VersionNumber g2pContextVersion;
        bool voicebankContext = false;     ///< true=voicebank private G2P, false=official G2P

        // S2P resource (srt::s2p::LanguageResource construction params)
        std::string s2pMode;               ///< "dict" | "direct"
        std::filesystem::path s2pFile;
        std::filesystem::path onsetFile;
    };

} // namespace ds::lang
