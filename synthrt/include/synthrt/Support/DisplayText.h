#ifndef SYNTHRT_DISPLAYTEXT_H
#define SYNTHRT_DISPLAYTEXT_H

#include <string>
#include <map>

#include <synthrt/Support/JSON.h>

namespace srt {

    /// DisplayText - Represents a text with multiple translations.
    class SYNTHRT_EXPORT DisplayText {
    public:
        /// Constructs an empty display text object.
        DisplayText();

        /// Constructs with a default text.
        DisplayText(std::string_view text);

        /// Constructs with a default text and a map, where the key is the locale code and the value
        /// is the corresponding text.
        DisplayText(std::string_view defaultText, const std::map<std::string, std::string> &texts);

        /// Constructs with a JSON value.
        /// \note The JSON value must be a string-mapping object, with the key being the locale code
        /// and the value being the corresponding text. If the \c _ property exists, use it as the
        /// default text; otherwise, search for the property of \c en_XX and try to use the value as
        /// the default text.
        explicit DisplayText(const JsonValue &value);

        ~DisplayText();

        DisplayText &operator=(std::string_view text);
        DisplayText &operator=(const JsonValue &value);

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