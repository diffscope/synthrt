#ifndef SYNTHRT_SUPPORT_DISPLAYTEXT_H
#define SYNTHRT_SUPPORT_DISPLAYTEXT_H

#include <string>
#include <map>
#include <memory>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Support/Expected.h>

namespace srt {

    /// A text with multiple translations.
    class SYNTHRT_EXPORT DisplayText {
    public:
        /// Constructs an empty display text object.
        DisplayText();

        /// Constructs with a default text.
        DisplayText(std::string text);

        /// Constructs with a default text and a map, where the key is the locale code and the value
        /// is the corresponding text.
        DisplayText(std::string defaultText, const std::map<std::string, std::string> &texts);

        ~DisplayText();

        inline void swap(DisplayText &RHS) noexcept {
            _impl.swap(RHS._impl);
        }

    public:
        /// Returns the default text.
        const std::string &text() const;

        /// Returns the text for \a locale, or an empty string when the locale is absent.
        const std::string &text(std::string_view locale) const;

        /// The locales this text has been translated into, not counting the default.
        ///
        /// \note Borrowed. The view lasts as long as this object does.
        stdc::array_view<std::string> locales() const;

        /// Returns whether the default text is empty.
        bool isEmpty() const;

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

}

#endif // SYNTHRT_SUPPORT_DISPLAYTEXT_H
