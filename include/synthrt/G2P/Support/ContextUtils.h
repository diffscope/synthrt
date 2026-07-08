#ifndef SRT_G2P_SUPPORT_CONTEXTUTILS_H
#define SRT_G2P_SUPPORT_CONTEXTUTILS_H

#include <string>
#include <string_view>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/ContextKey.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p {

    /// FqidParseResult - parsed components of a fully-qualified module id.
    struct FqidParseResult {
        std::string context;
        stdc::VersionNumber version;    ///< Parsed from "context@version:moduleId"
        std::string moduleId;
    };

    /// ContextUtils - G2P-specific FQID parsing and validation helpers.
    ///
    /// Migrated from LangCore::ContextUtils. Uses srt::core::ContextKey as the
    /// composite key type (ContextKey itself is generic and lives in srt-core).
    ///
    /// FQID format: [context[@version]:]moduleId
    ///   "g2p-cmn"               → {"", {}, "g2p-cmn"}
    ///   ":g2p-cmn"              → {"", {}, "g2p-cmn"}
    ///   "SingerA:g2p-cmn"       → {"SingerA", {}, "g2p-cmn"}
    ///   "SingerA@2.0.0:g2p-cmn" → {"SingerA", 2.0.0, "g2p-cmn"}
    ///   "A:B:C"                 → {"A", {}, "B:C"} (first colon separates)
    class SRT_G2P_EXPORT ContextUtils {
    public:
        /// Parse FQID string into context + version + moduleId.
        static FqidParseResult parseFqid(const std::string_view &fqid) {
            FqidParseResult result;
            auto colonPos = fqid.find(':');
            if (colonPos == std::string_view::npos) {
                // No colon: plain moduleId
                result.moduleId = std::string(fqid);
            } else {
                auto contextPart = fqid.substr(0, colonPos);
                result.moduleId = std::string(fqid.substr(colonPos + 1));

                // Check for '@' in context part → version
                auto atPos = contextPart.find('@');
                if (atPos == std::string_view::npos) {
                    result.context = std::string(contextPart);
                } else {
                    result.context = std::string(contextPart.substr(0, atPos));
                    result.version =
                        stdc::VersionNumber::fromString(std::string(contextPart.substr(atPos + 1)));
                }
            }
            return result;
        }

        /// Format ContextKey + moduleId into FQID string.
        /// ({"SingerA", 2.0.0}, "g2p-cmn") → "SingerA@2.0.0:g2p-cmn"
        /// ({"SingerA", {}}, "g2p-cmn") → "SingerA:g2p-cmn"
        /// ({"", {}}, "g2p-cmn") → "g2p-cmn"
        static std::string formatFqid(const srt::core::ContextKey &ctxKey,
                                      const std::string_view &moduleId) {
            if (ctxKey.isDefault())
                return std::string(moduleId);
            return ctxKey.toString() + ":" + std::string(moduleId);
        }

        /// Maximum allowed context name length (T7: centralized constant).
        static constexpr size_t kMaxContextNameLength = 128;

        /// Validate context name. Empty string is valid (default context).
        /// Allowed chars: [A-Za-z0-9_.-]
        /// Max length: kMaxContextNameLength
        /// Returns Error on invalid.
        static srt::core::Expected<void> validateContextName(const std::string_view &context) {
            if (context.empty())
                return {}; // default context is always valid

            if (context.size() > kMaxContextNameLength) {
                return Error(Error::ValidationError,
                             "Context name '" + std::string(context.substr(0, 20)) +
                                 "...' exceeds maximum length (" +
                                 std::to_string(kMaxContextNameLength) + ")");
            }

            for (size_t i = 0; i < context.size(); ++i) {
                char ch = context[i];
                bool valid = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                             (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-';
                if (!valid) {
                    return Error(Error::ValidationError,
                                 "Invalid context name '" + std::string(context) +
                                     "': contains forbidden character '" + std::string(1, ch) +
                                     "'. Allowed: [A-Za-z0-9_.-]");
                }
            }
            return {};
        }

        /// Validate that a moduleId does not contain ':' (reserved for FQID separation).
        static srt::core::Expected<void> validateModuleId(const std::string_view &moduleId) {
            if (moduleId.find(':') != std::string_view::npos) {
                return Error(Error::ValidationError,
                             "Module ID '" + std::string(moduleId) +
                                 "' contains ':' which is reserved for context separation");
            }
            return {};
        }
    };

} // namespace srt::g2p

#endif // SRT_G2P_SUPPORT_CONTEXTUTILS_H
