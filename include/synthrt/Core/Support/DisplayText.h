#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    /// DisplayText - Represents a text with multiple translations.
    ///
    /// Migrated from \c srt::DisplayText (synthrt/Support/DisplayText.h) to
    /// \c srt::core::DisplayText, and aligned with the ds-spec 2.4 多语言文本
    /// rules:
    ///   - A JSON string is the short form of an object carrying only the
    ///     default text. An object must contain the default entry \c "_";
    ///     every other key is an opaque, case-sensitive language tag whose
    ///     meaning is a private contract between the content author and the
    ///     front-end (BCP 47 recommended but NOT enforced).
    ///   - The Runtime performs NO matching: no Lookup, no case folding, no
    ///     fallback. text(key) is an exact key lookup; locales() exposes the
    ///     complete key set verbatim. How to match a user's language
    ///     preference against the keys, and when to use the default text, is
    ///     entirely the front-end's decision.
    ///
    /// Used by \c InferenceSpec, \c SingerSpec and package metadata to carry
    /// localized display names.
    class SRT_CORE_EXPORT DisplayText {
    public:
        /// Constructs an empty display text object.
        DisplayText();

        /// Constructs with a default text.
        DisplayText(std::string text);

        /// Constructs with a default text from a string literal (single
        /// user-defined conversion so setters accept literals directly).
        DisplayText(const char *text);

        /// Constructs with a default text and a map, where the key is the
        /// opaque language tag and the value is the corresponding text. A \c
        /// "_" entry in \p texts, if any, is ignored (the default text always
        /// comes from \p defaultText).
        DisplayText(std::string defaultText, const std::map<std::string, std::string> &texts);

        ~DisplayText();

        /// Assigns a new default text; the translations are left in place.
        DisplayText &operator=(std::string text);

        /// Assigns a new default text from a literal; translations are left in
        /// place. (Overloads keep literal assignment unambiguous.)
        DisplayText &operator=(const char *text);

        /// Strict parsing per ds-spec 2.4: the JSON value must be a string or
        /// an object with a mandatory string \c "_" entry; every other entry
        /// must also be a string. Returns an \c InvalidFormat error otherwise.
        static Expected<DisplayText> fromJsonValue(const JsonValue &value);

        /// Tolerant parsing for scanning untrusted or legacy packages: never
        /// fails. A string is used verbatim; a non-string/non-object yields an
        /// empty text. When the object lacks \c "_", the default text is
        /// selected from "default" / "en" / the first string entry, in that
        /// order (matching the legacy no-locale resolution). Entries with
        /// non-string values are skipped rather than rejected.
        static DisplayText fromJsonValueTolerant(const JsonValue &value);

        inline void swap(DisplayText &RHS) noexcept {
            _impl.swap(RHS._impl);
        }

    public:
        /// The default text (\c "_" entry).
        const std::string &text() const;

        /// The text stored under exactly \p key, or \c nullptr if the key is
        /// absent. The comparison is bytewise (case-sensitive) and there is
        /// no fallback to the default text; \c "_" never matches here because
        /// it is the default entry, not a translation key.
        const std::string *text(std::string_view key) const;

        /// Every translation key this text carries, excluding the \c "_"
        /// default entry, verbatim (no normalization), in deterministic
        /// sorted order.
        ///
        /// \note Borrowed. The view lasts as long as this object does.
        stdc::array_view<std::string> locales() const;

        bool isEmpty() const;

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

}
