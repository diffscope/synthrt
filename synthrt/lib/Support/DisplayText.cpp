#include "DisplayText.h"

#include <stdcorelib/pimpl.h>

namespace srt {

    class DisplayText::Impl {
    public:
        std::string defaultText;
        std::optional<std::map<std::string, std::string, std::less<>>> texts;

        void assign(const JsonValue &value) {
            if (value.isString()) {
                defaultText = value.toString();
                return;
            }
            if (!value.isObject()) {
                return;
            }
            const auto &obj = value.toObject();
            std::string defaultText_;
            std::map<std::string, std::string, std::less<>> texts_;
            for (const auto &item : obj) {
                if (item.first == "_") {
                    defaultText = item.second.toString();
                    continue;
                }
                texts_[item.first] = item.second.toString();
            }

            if (!texts_.empty()) {
                static const char *candidates[] = {
                    "en", "en_US", "en_us", "en_GB", "en_gb",
                };
                for (const auto &item : candidates) {
                    if (!defaultText_.empty()) {
                        break;
                    }
                    auto it = texts_.find(item);
                    if (it != texts_.end()) {
                        defaultText_ = it->second;
                    }
                }
                if (defaultText_.empty()) {
                    return;
                }
                texts = std::move(texts_);
            }
            defaultText = std::move(defaultText_);
        }
    };

    DisplayText::DisplayText() : _impl(std::make_shared<Impl>()) {
    }

    DisplayText::DisplayText(std::string text) : _impl(std::make_shared<Impl>()) {
        __stdc_impl_t;
        impl.defaultText = std::move(text);
    }

    DisplayText::DisplayText(std::string defaultText,
                             const std::map<std::string, std::string> &texts)
        : DisplayText(std::move(defaultText)) {
        __stdc_impl_t;
        impl.texts = {texts.begin(), texts.end()};
    }

    DisplayText::DisplayText(const JsonValue &value) : _impl(std::make_shared<Impl>()) {
        __stdc_impl_t;
        impl.assign(value);
    }

    DisplayText::~DisplayText() = default;

    DisplayText &DisplayText::operator=(std::string text) {
        __stdc_impl_t;
        impl.defaultText = std::move(text);
        return *this;
    }

    DisplayText &DisplayText::operator=(const JsonValue &value) {
        __stdc_impl_t;
        impl.assign(value);
        return *this;
    }

    const std::string &DisplayText::text() const {
        __stdc_impl_t;
        return impl.defaultText;
    }

    const std::string &DisplayText::text(std::string_view locale) const {
        __stdc_impl_t;
        if (!impl.texts) {
            return impl.defaultText;
        }
        auto it = impl.texts->find(locale);
        if (it == impl.texts->end()) {
            return impl.defaultText;
        }
        return it->second;
    }

    bool DisplayText::isEmpty() const {
        __stdc_impl_t;
        return impl.defaultText.empty();
    }

}