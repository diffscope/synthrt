#ifndef SYNTHRT_DISPLAYTEXT_H
#define SYNTHRT_DISPLAYTEXT_H

#include <string>
#include <map>
#include <memory>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

namespace srt {

    /// DisplayText - Represents a text with multiple translations.
    class SYNTHRT_EXPORT DisplayText {
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

#endif // SYNTHRT_DISPLAYTEXT_H
