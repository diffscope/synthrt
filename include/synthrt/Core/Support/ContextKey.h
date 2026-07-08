#ifndef SRT_CORE_SUPPORT_CONTEXTKEY_H
#define SRT_CORE_SUPPORT_CONTEXTKEY_H

#include <string>
#include <string_view>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    /// ContextKey - composite key for context namespace: (name, version).
    ///
    /// Migrated from LangCore::ContextKey (part of ContextUtils.h). The struct
    /// itself is generic (used by srt-core::ModuleSpec), while the FQID
    /// parsing/validation helpers live in srt::g2p::ContextUtils.
    ///
    /// Semantics:
    ///   - Empty context + empty version = default context
    ///   - Non-empty context + empty version = unversioned context
    ///   - Non-empty context + non-empty version = versioned context
    struct SRT_CORE_EXPORT ContextKey {
        std::string context;            ///< Voice bank name ("" = default context)
        stdc::VersionNumber version;    ///< Voice bank version (isEmpty() = unversioned)

        ContextKey() = default;
        explicit ContextKey(std::string context, stdc::VersionNumber version = {})
            : context(std::move(context)), version(std::move(version)) {}

        bool operator<(const ContextKey &o) const {
            if (context != o.context)
                return context < o.context;
            return version < o.version;
        }

        bool operator==(const ContextKey &o) const {
            return context == o.context && version == o.version;
        }

        bool operator!=(const ContextKey &o) const { return !(*this == o); }

        /// Whether this context has a version
        bool isVersioned() const { return !version.isEmpty(); }

        /// Whether this is the default context (empty name, no version)
        bool isDefault() const { return context.empty() && version.isEmpty(); }

        /// Human-readable string:
        ///   "" → "(default)"
        ///   "SingerA" → "SingerA"
        ///   "SingerA" + 2.0.0 → "SingerA@2.0.0"
        std::string toString() const {
            if (context.empty() && version.isEmpty())
                return "(default)";
            if (version.isEmpty())
                return context;
            return context + "@" + version.toString();
        }
    };

} // namespace srt::core

#endif // SRT_CORE_SUPPORT_CONTEXTKEY_H
