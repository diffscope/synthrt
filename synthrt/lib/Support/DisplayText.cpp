#include "DisplayText.h"

#include <stdcorelib/pimpl.h>

namespace srt {

    class DisplayText::Impl {
    public:
        std::string defaultText;
        std::optional<std::map<std::string, std::string, std::less<>>> texts;

        void clear() {
            defaultText.clear();
            texts.reset();
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
        auto exp = fromJsonValue(value);
        if (exp) {
            swap(exp.get());
        }
    }

    DisplayText::~DisplayText() = default;

    DisplayText &DisplayText::operator=(std::string text) {
        __stdc_impl_t;
        impl.defaultText = std::move(text);
        return *this;
    }

    DisplayText &DisplayText::operator=(const JsonValue &value) {
        __stdc_impl_t;
        auto exp = fromJsonValue(value);
        if (!exp) {
            impl.clear();
            return *this;
        }
        swap(exp.get());
        return *this;
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
