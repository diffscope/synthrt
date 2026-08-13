#include "DisplayText.h"

#include <stdcorelib/pimpl.h>

#include <optional>

namespace srt::core {

    class DisplayText::Impl {
    public:
        std::string                                                    defaultText;
        std::optional<std::map<std::string, std::string, std::less<>>> texts;

        void clear() {
            defaultText.clear();
            texts.reset();
        }
    };

    DisplayText::DisplayText() : _impl(std::make_shared<Impl>()) {
    }

    DisplayText::DisplayText(std::string text) : _impl(std::make_shared<Impl>()) {
        stdc_impl_t;
        impl.defaultText = std::move(text);
    }

    DisplayText::DisplayText(std::string defaultText, const std::map<std::string, std::string> &texts)
        : DisplayText(std::move(defaultText)) {
        stdc_impl_t;
        impl.texts = {texts.begin(), texts.end()};
    }

    DisplayText::~DisplayText() = default;

    DisplayText &DisplayText::operator=(std::string text) {
        stdc_impl_t;
        impl.defaultText = std::move(text);
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

        const auto &obj       = value.toObject();
        auto        itDefault = obj.find("_");
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
        stdc_impl_t;
        return impl.defaultText;
    }

    const std::string &DisplayText::text(std::string_view locale) const {
        stdc_impl_t;
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
        stdc_impl_t;
        return impl.defaultText.empty();
    }

} // namespace srt::core
