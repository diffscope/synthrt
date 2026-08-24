#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    /// DisplayPath - The multi-language path counterpart of \c DisplayText.
    ///
    /// Same ds-spec 2.4 pass-through model: keys are opaque, case-sensitive
    /// map keys; the Runtime performs no matching (no Lookup, no case
    /// folding, no fallback). Path values are expected to have been resolved
    /// against their declaring file by the parser before storage.
    class SRT_CORE_EXPORT DisplayPath {
    public:
        DisplayPath();
        DisplayPath(std::filesystem::path path);
        /// A \c "_" entry in \p paths, if any, is ignored (the default path
        /// always comes from \p defaultPath).
        DisplayPath(std::filesystem::path defaultPath,
                    const std::map<std::string, std::filesystem::path> &paths);
        ~DisplayPath();

        DisplayPath &operator=(std::filesystem::path path);

        static Expected<DisplayPath> fromJsonValue(const JsonValue &value);

        void swap(DisplayPath &RHS) noexcept;

    public:
        /// The default path (\c "_" entry).
        const std::filesystem::path &path() const;

        /// The path stored under exactly \p key, or \c nullptr if absent
        /// (bytewise, case-sensitive; no fallback to the default path).
        const std::filesystem::path *path(std::string_view key) const;

        /// Every translation key this path carries, excluding the \c "_"
        /// default entry, verbatim, in deterministic sorted order.
        ///
        /// \note Borrowed. The view lasts as long as this object does.
        stdc::array_view<std::string> locales() const;

        bool isEmpty() const;

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

}
