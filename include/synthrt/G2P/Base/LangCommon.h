#ifndef SRT_G2P_BASE_LANGCOMMON_H
#define SRT_G2P_BASE_LANGCOMMON_H

#include <string>
#include <utility>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p {

    // --- Cross-project contract constants (T7) ---
    // Referenced by ds-editor-lite; defined here as the single source of truth.
    inline constexpr auto kG2pOnnxDriverName = "g2pOnnxDriver"; // ONNX 驱动裸名（跨项目契约）
    inline constexpr auto kOfficialContext = "";                 // 官方默认 context 标识
    inline constexpr auto kG2pSourceOfficial = "official";        // g2pSource: 官方默认上下文
    inline constexpr auto kG2pSourceVoicebank = "voicebank";     // g2pSource: 声库私有上下文

    // --- Framework category name constants (T7) ---
    inline constexpr auto kG2pCategory = "g2p";
    inline constexpr auto kDictCategory = "dict";
    inline constexpr auto kDriverCategory = "driver";

    // --- G2pRes mode 字符串常量 ---
    // "convert"：经模型/字典真正转换得到发音；
    // "copy"：原词保留（回退、降级、标点/数字等未真正转换）；
    // "skip"：空 lyric 跳过（pronunciation 为空）。
    inline constexpr auto kG2pModeConvert = "convert";
    inline constexpr auto kG2pModeCopy = "copy";
    inline constexpr auto kG2pModeSkip = "skip";

    // --- Plugin IID constants (P3.2: migrated to srt.g2p.* IIDs) ---
    inline constexpr auto kTaskPluginIid = "srt.g2p.task";
    inline constexpr auto kDriverPluginIid = "srt.g2p.driver";

    struct TaggerRes {
        std::string lyric;
        std::string language = "unknown";
        std::string tag = "unknown";
        bool discard = false;

        explicit TaggerRes(std::string lyric) : lyric(std::move(lyric)) {}
        explicit TaggerRes(std::string lyric, std::string language, std::string tag) :
            lyric(std::move(lyric)), language(std::move(language)), tag(std::move(tag)) {}
    };

    struct G2pInput {
        std::string lyric;
        std::string g2pId;
        std::string g2pContext;                    // Voice bank name (empty = default context)
        stdc::VersionNumber g2pContextVersion;     // Voice bank version (isEmpty() = unversioned)

        G2pInput() = default;
        G2pInput(std::string lyric, std::string g2pId, std::string g2pContext = {},
                 stdc::VersionNumber g2pContextVersion = {})
            : lyric(std::move(lyric)), g2pId(std::move(g2pId)), g2pContext(std::move(g2pContext)),
              g2pContextVersion(std::move(g2pContextVersion)) {}
    };

    enum G2pErrorType {
        NoError = 0,
        InvalidLyric,
        ModelInferenceFailed,
        PhonemeGenerationFailed,
        DriverUnavailable,
        NotInitialized,
        UnknownError,
    };

    /// G2pRes - G2P conversion result (output side, D14: keeps g2pContext/
    /// g2pContextVersion/g2pSource for UI display).
    struct G2pRes {
        std::string lyric;
        std::string g2pId;
        std::string g2pContext;
        stdc::VersionNumber g2pContextVersion;     // Voice bank version (isEmpty() = unversioned)
        std::string g2pSource;                     // Source context: "official" (default) or "voicebank"
        std::string pronunciation;
        std::vector<std::string> candidates;
        std::string mode = kG2pModeCopy;
        G2pErrorType errorType = NoError;

        /// 是否未发生错误（含合法的原词保留，如标点/数字）。
        /// true 表示 errorType == NoError（mode 可能是 "convert" / "copy" / "skip"）。
        /// false 表示推理失败（errorType != NoError），调用方应考虑回退。
        /// 注：若需区分"真正转换"与"原词保留"，额外检查 mode == "convert"。
        bool isOk() const { return errorType == NoError; }

        /// 是否为推理失败兜底（需回退的场景）。
        /// 等价于 !isOk()。
        bool isFailed() const { return errorType != NoError; }

        G2pRes() {}

        G2pRes(std::string lyric, std::string g2pId, std::string g2pContext = {},
               stdc::VersionNumber g2pContextVersion = {}, std::string pronunciation = {},
               std::vector<std::string> candidates = {}, std::string mode = kG2pModeCopy,
               const G2pErrorType errorType = NoError, std::string g2pSource = {}) :
            lyric(std::move(lyric)), g2pId(std::move(g2pId)), g2pContext(std::move(g2pContext)),
            g2pContextVersion(std::move(g2pContextVersion)), g2pSource(std::move(g2pSource)),
            pronunciation(std::move(pronunciation)), candidates(std::move(candidates)),
            mode(std::move(mode)), errorType(errorType) {
            // Order matters: apply the lyric fallback BEFORE seeding
            // candidates from pronunciation. Otherwise when pronunciation
            // is empty the candidates push_back is skipped (pronunciation
            // is still empty at that point), and the later fallback sets
            // pronunciation=lyric but leaves candidates empty, breaking
            // callers that iterate candidates uniformly.
            if (this->pronunciation.empty())
                this->pronunciation = this->lyric;

            if (this->candidates.empty() && !this->pronunciation.empty())
                this->candidates.push_back(this->pronunciation);
        }
    };

} // namespace srt::g2p

#endif // SRT_G2P_BASE_LANGCOMMON_H
