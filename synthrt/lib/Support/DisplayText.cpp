#include "DisplayText.h"

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/pimpl.h>

namespace srt {

    class DisplayText::Impl {
    public:
        std::string defaultText;

        /// The locale codes, owned here so that \c locales() has something to hand out and the
        /// map below has something to key into.
        ///
        /// \note Nothing may be added once \c texts refers into this. A locale code is short
        ///       enough to live in the string's own small buffer, so relocating one during a
        ///       regrow carries its characters along and leaves every key dangling.
        stdc::vlarray<std::string, 4> locales;

        /// Keyed by a view into \c locales rather than by a second copy of the same code.
        std::map<std::string_view, std::string, std::less<>> texts;
    };

    DisplayText::DisplayText() : _impl(std::make_shared<Impl>()) {
    }

    DisplayText::DisplayText(std::string text) : _impl(std::make_shared<Impl>()) {
        stdc_impl_t;
        impl.defaultText = std::move(text);
    }

    DisplayText::DisplayText(std::string defaultText,
                             const std::map<std::string, std::string> &texts)
        : DisplayText(std::move(defaultText)) {
        stdc_impl_t;

        // Sized to its final length before a single view into it is taken, so that no push below
        // can relocate what an earlier key already points at.
        impl.locales.reserve(texts.size());
        for (const auto &item : texts) {
            impl.locales.push_back(item.first);
            impl.texts.emplace(impl.locales.back(), item.second);
        }
    }

    DisplayText::~DisplayText() = default;

    const std::string &DisplayText::text() const {
        stdc_impl_t;
        return impl.defaultText;
    }

    const std::string &DisplayText::text(std::string_view locale) const {
        stdc_impl_t;
        auto it = impl.texts.find(locale);
        if (it == impl.texts.end()) {
            return impl.defaultText;
        }
        return it->second;
    }

    stdc::array_view<std::string> DisplayText::locales() const {
        stdc_impl_t;
        return {impl.locales.data(), impl.locales.size()};
    }

    bool DisplayText::isEmpty() const {
        stdc_impl_t;
        return impl.defaultText.empty();
    }

}
