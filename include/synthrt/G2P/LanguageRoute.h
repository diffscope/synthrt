#pragma once

#include <filesystem>
#include <string>

#include <stdcorelib/support/versionnumber.h>

namespace srt::g2p {

    /// LanguageRoute - G2P route + S2P resource, returned by
    /// LanguageService::resolveLanguageRoute().
    ///
    /// Combines G2P routing info and S2P resource references so the host
    /// (CLI/lite) doesn't need to call lower-level resolve methods.
    ///
    /// Field semantics (R7):
    ///   - g2pContext: G2P context name. Empty (= kOfficialContext) means the
    ///     official default context; otherwise the voicebank private context
    ///     (typically the singerId of the voicebank).
    ///   - g2pSource: "official" or "voicebank" (see kG2pSourceOfficial /
    ///     kG2pSourceVoicebank in LangCommon.h).
    struct LanguageRoute {
        std::string g2pId;                 ///< Actual G2P plugin task id
        std::string g2pContext;            ///< G2P context name (empty = official default context)
        stdc::VersionNumber g2pContextVersion;
        std::string g2pSource;             ///< Source: "official" / "voicebank"

        // S2P resource (srt::s2p::LanguageResource construction params)
        std::string s2pMode;               ///< "dict" | "direct"
        std::filesystem::path s2pFile;
        std::filesystem::path onsetFile;
    };

} // namespace srt::g2p
