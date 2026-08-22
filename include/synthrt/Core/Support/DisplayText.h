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
    /// \c srt::core::DisplayText, and upgraded to the ds-spec 2.4 多语言文本
    /// rules:
    ///   - A JSON string is the short form of an object carrying only the
    ///     default text. An object must contain the default entry \c "_" (the
    ///     value taken when no language matches); every other key is a BCP 47
    ///     language tag.
    ///   - text(locale) performs RFC 4647 Lookup: try the full tag, then
    ///     repeatedly strip the rightmost subtag until only the language
    ///     subtag remains, then fall back to the default text. Matching is
    ///     case-insensitive; the separator is strictly \c '-' (POSIX-style
    ///     \c zh_CN keys are NOT recognized).
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

        /// Constructs with a default text and a map, where the key is the BCP 47
        /// language tag and the value is the corresponding text.
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

        /// The text for \p locale, resolved by RFC 4647 Lookup.
        const std::string &text(std::string_view locale) const;

        /// The BCP 47 tags this text has been translated into, not counting
        /// the default.
        ///
        /// \note Borrowed. The view lasts as long as this object does.
        stdc::array_view<std::string> locales() const;

        bool isEmpty() const;

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

}
