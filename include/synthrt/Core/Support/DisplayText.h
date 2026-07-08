#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    /// DisplayText - Represents a text with multiple translations.
    ///
    /// Migrated from \c srt::DisplayText (synthrt/Support/DisplayText.h) to
    /// \c srt::core::DisplayText. Used by \c InferenceSpec, \c SingerSpec and
    /// package metadata to carry localized display names.
    class SRT_CORE_EXPORT DisplayText {
    public:
        /// Constructs an empty display text object.
        DisplayText();

        /// Constructs with a default text.
        DisplayText(std::string text);

        /// Constructs with a default text and a map, where the key is the locale code and the value
        /// is the corresponding text.
        DisplayText(std::string defaultText, const std::map<std::string, std::string> &texts);

        /// Constructs with a JSON value.
        /// \deprecated Use fromJsonValue() instead.
        [[deprecated("Use DisplayText::fromJsonValue() instead.")]]
        explicit DisplayText(const JsonValue &value);

        ~DisplayText();

        DisplayText &operator=(std::string text);

        [[deprecated("Use DisplayText::fromJsonValue() instead.")]]
        DisplayText &operator=(const JsonValue &value);

        static Expected<DisplayText> fromJsonValue(const JsonValue &value);

        inline void swap(DisplayText &RHS) noexcept {
            _impl.swap(RHS._impl);
        }

    public:
        const std::string &text() const;
        const std::string &text(std::string_view locale) const;

        bool isEmpty() const;

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

}
