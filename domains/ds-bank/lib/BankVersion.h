#pragma once

#include <optional>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

namespace ds::bank {

    // DiffSinger voicebank versions are 1..4 numeric segments (e.g. "26.1.5.0").
    // The pinned stdcorelib VersionNumber::fromString accepts only 1..4 segments
    // and returns nullopt beyond that (its former "trim extra segments" behavior
    // was removed), so "1.2.3.4.5" or a query like "1.2.3.4.0" would not parse.
    // This helper trims to the first 4 segments before parsing, so 5-segment
    // versions keep their first 4 segments ("1.2.3.4.5" -> 1.2.3.4) and
    // "1.2.3.4.0" matches "1.2.3.4". Used by PackageParser and VoicebankScanner;
    // does not rely on stdcorelib tolerance. Non-numeric segments (e.g. 'v'
    // prefixes, "1.a") return nullopt, preserving the legacy string-compare /
    // empty-version fallback semantics.
    inline std::optional<stdc::VersionNumber> parseBankVersion(const std::string &text) {
        if (text.empty()) {
            return std::nullopt;
        }

        std::vector<std::string> parts;
        std::string current;
        for (const char c : text) {
            if (c != '.') {
                current.push_back(c);
                continue;
            }
            parts.push_back(current);
            current.clear();
        }
        parts.push_back(current);
        if (parts.size() > 4) {
            parts.resize(4);
        }

        std::string normalized;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                normalized.push_back('.');
            }
            normalized.append(parts[i]);
        }
        return stdc::VersionNumber::fromString(normalized);
    }

} // namespace ds::bank
