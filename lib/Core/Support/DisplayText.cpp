#include "DisplayText.h"

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/pimpl.h>

namespace srt::core {

    class DisplayText::Impl {
    public:
        std::string defaultText;

        /// The language tags, owned here so that \c locales() has something to hand out and the
        /// map below has something to key into.
        ///
        /// \note Nothing may be added once \c texts refers into this. A language tag is short
        ///       enough to live in the string's own small buffer, so relocating one during a
        ///       regrow carries its characters along and leaves every key dangling.
        stdc::vlarray<std::string, 4> locales;

        /// Keyed by a view into \c locales rather than by a second copy of the same tag.
        std::map<std::string_view, std::string, std::less<>> texts;
    };

    DisplayText::DisplayText() : _impl(std::make_shared<Impl>()) {
    }

    DisplayText::DisplayText(std::string text) : _impl(std::make_shared<Impl>()) {
        stdc_impl_t;
        impl.defaultText = std::move(text);
    }

    DisplayText::DisplayText(const char *text) : DisplayText(std::string(text ? text : "")) {
    }

    DisplayText::DisplayText(std::string defaultText,
                             const std::map<std::string, std::string> &texts)
        : DisplayText(std::move(defaultText)) {
        stdc_impl_t;

        // Sized to its final length before a single view into it is taken, so that no push below
        // can relocate what an earlier key already points at.
        impl.locales.reserve(texts.size());
        for (const auto &item : texts) {
            // "_" is the default entry of the JSON object form, not a
            // translation key: keep it out of locales()/text(key) even if a
            // direct caller passes it in the map.
            if (item.first == "_") {
                continue;
            }
            impl.locales.push_back(item.first);
            impl.texts.emplace(impl.locales.back(), item.second);
        }
    }

    DisplayText::~DisplayText() = default;

    DisplayText &DisplayText::operator=(std::string text) {
        stdc_impl_t;
        impl.defaultText = std::move(text);
        return *this;
    }

    DisplayText &DisplayText::operator=(const char *text) {
        return *this = std::string(text ? text : "");
    }

    Expected<DisplayText> DisplayText::fromJsonValue(const JsonValue &value) {
        if (value.isString()) {
            return DisplayText(value.toString());
        }

        if (!value.isObject()) {
            return Error{
                Error::InvalidFormat,
                R"(must be a string or an object)",
            };
        }

        const auto &obj = value.toObject();
        auto itDefault = obj.find("_");
        if (itDefault == obj.end()) {
            return Error{
                Error::InvalidFormat,
                R"(must contain "_" field)",
            };
        }
        if (!itDefault->second.isString()) {
            return Error{
                Error::InvalidFormat,
                R"("_" field must be a string)",
            };
        }

        std::map<std::string, std::string> texts;
        for (const auto &item : obj) {
            if (!item.second.isString()) {
                return Error{
                    Error::InvalidFormat,
                    R"(field ")" + item.first + R"(" must be a string)",
                };
            }
            if (item.first != "_") {
                texts[item.first] = item.second.toString();
            }
        }

        return DisplayText(itDefault->second.toString(), texts);
    }

    DisplayText DisplayText::fromJsonValueTolerant(const JsonValue &value) {
        if (value.isString()) {
            return DisplayText(value.toString());
        }
        if (!value.isObject()) {
            return DisplayText();
        }

        const auto &obj = value.toObject();
        std::string defaultText;
        std::map<std::string, std::string> texts;
        for (const auto &item : obj) {
            if (!item.second.isString()) {
                continue; // skip rather than reject
            }
            if (item.first == "_") {
                defaultText = item.second.toString();
            } else {
                texts[item.first] = item.second.toString();
            }
        }

        // No "_" entry (legacy packages): select the default text by the
        // historical no-locale resolution order — "default", "en", then the
        // first entry in (sorted) key order.
        if (obj.find("_") == obj.end()) {
            for (const char *key : {"default", "en"}) {
                if (auto it = texts.find(key); it != texts.end()) {
                    defaultText = it->second;
                    break;
                }
            }
            if (defaultText.empty() && !texts.empty()) {
                defaultText = texts.begin()->second;
            }
        }

        return DisplayText(std::move(defaultText), texts);
    }

    const std::string &DisplayText::text() const {
        stdc_impl_t;
        return impl.defaultText;
    }

    const std::string *DisplayText::text(std::string_view key) const {
        stdc_impl_t;

        // ds-spec 2.4 多语言文本: keys are opaque and case-sensitive, and the
        // Runtime performs no matching. This is a plain exact key lookup with
        // no fallback to the default text; how to map a language preference
        // onto the available keys is entirely the front-end's business.
        const auto it = impl.texts.find(key);
        if (it == impl.texts.end()) {
            return nullptr;
        }
        return &it->second;
    }

    stdc::array_view<std::string> DisplayText::locales() const {
        stdc_impl_t;
        return {impl.locales.data(), impl.locales.size()};
    }

    bool DisplayText::isEmpty() const {
        stdc_impl_t;
        return impl.defaultText.empty();
    }

} // namespace srt::core
