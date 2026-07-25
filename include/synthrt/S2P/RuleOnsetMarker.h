#pragma once

#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include <synthrt/S2P/srt_s2p_global.h>

namespace srt::s2p {

    /// RuleOnsetMarker marks onset positions in a phoneme sequence using a
    /// JSON rule definition (phonemeTypes + rules with pattern/onsets arrays).
    ///
    /// The matcher uses a trie keyed by (exact | typed-wildcard | "*") terms
    /// and picks the longest, most-specific matching path. `mark` is a `const`
    /// instance method because the rule set is loaded once at construction.
    class SRT_S2P_EXPORT RuleOnsetMarker {
    public:
        /// Creates a RuleOnsetMarker from a JSON rule definition stream.
        /// Returns an `Expected` error (InvalidFormat) on malformed input
        /// instead of throwing.
        static srt::core::Expected<std::unique_ptr<RuleOnsetMarker>> create(std::istream &ruleDefinitionFile);

        ~RuleOnsetMarker();

        RuleOnsetMarker(const RuleOnsetMarker &) = delete;
        RuleOnsetMarker &operator=(const RuleOnsetMarker &) = delete;
        RuleOnsetMarker(RuleOnsetMarker &&) noexcept;
        RuleOnsetMarker &operator=(RuleOnsetMarker &&) noexcept;

        std::vector<bool> mark(const std::vector<std::string> &phonemeSequence) const;

    private:
        RuleOnsetMarker();

        class Private;
        std::unique_ptr<Private> d;
    };

}
