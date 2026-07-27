#pragma once

#include <memory>
#include <source_location>
#include <string>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p {

    using srt::core::ErrorCode;

    /// Error - G2P domain error type.
    ///
    /// Inherits from srt::core::Error to allow seamless use with
    /// srt::core::Expected<T> (slicing is safe: same layout, no virtual
    /// members, no additional data members beyond _suggestion).
    ///
    /// Uses srt::core::ErrorCode (G2p* codes, 300-399) for error coding.
    /// The legacy Type enum was removed in Level=3 (2026-07-28); all
    /// callers must use ErrorCode-based constructors.
    ///
    /// Migrated from LangCore::Error (12 types preserved per D11/D12).
    class SRT_G2P_EXPORT Error : public srt::core::Error {
    public:
        // === Constructors (ErrorCode + auto source_location) ===

        Error(ErrorCode code, std::string msg,
              const std::source_location &loc = std::source_location::current());

        Error(ErrorCode code, const char *msg,
              const std::source_location &loc = std::source_location::current());

        /// Constructor with suggestion (migrated from LangCore::Error).
        Error(ErrorCode code, std::string msg, std::string suggestion,
              const std::source_location &loc = std::source_location::current());

        Error(ErrorCode code, const char *msg, const char *suggestion,
              const std::source_location &loc = std::source_location::current());

        // === Default constructor ===
        Error() : Error(ErrorCode::G2pSuccess, std::string{}) {}

        /// G2P-specific ok check (unified with base class).
        /// G2pSuccess is mapped to NoError by typeFromCode (ER-02 fix), so this
        /// returns true for both G2pSuccess and ErrorCode::None, matching the
        /// base class _type == NoError semantics.
        bool ok() const { return srt::core::Error::ok(); }

        /// Suggestion accessor (returns empty string if no suggestion is set).
        const std::string &suggestion() const {
            static const std::string emptySuggestion;
            return _suggestion ? *_suggestion : emptySuggestion;
        }

        bool hasSuggestion() const { return _suggestion != nullptr; }

        static Error success() { return Error(ErrorCode::G2pSuccess, std::string{}); }

    protected:
        std::shared_ptr<std::string> _suggestion;
    };

} // namespace srt::g2p
