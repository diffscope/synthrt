#include "Support/DisplayText.h"

#include <stdcorelib/pimpl.h>

#include <optional>
#include <utility>

namespace srt::g2p {

    class DisplayText::Impl {
    public:
        std::string                                                    m_defaultText;
        std::optional<std::map<std::string, std::string, std::less<>>> m_texts;

        void assign(const srt::core::JsonValue &value) {
            if (value.isString()) {
                m_defaultText = value.toString();
                return;
            }
            if (!value.isObject()) {
                return;
            }
            const auto                                     &obj = value.toObject();
            std::string                                     defaultText_;
            std::map<std::string, std::string, std::less<>> texts_;
            for (const auto &[fst, snd] : obj) {
                if (fst == "_") {
                    defaultText_ = snd.toString();
                    continue;
                }
                texts_[fst] = snd.toString();
            }

            if (!texts_.empty()) {
                // 默认语言候选顺序：英语、中文、日语
                static const char *candidates[] = {
                    "en", "en_US", "en_us", "en_GB",   "en_gb", // 英语
                    "zh", "zh_CN", "zh_cn", "zh-Hans",          // 中文（简体）
                    "ja", "ja_JP", "ja_jp", "ja-JP"             // 日语
                };
                for (const auto &item : candidates) {
                    if (!defaultText_.empty()) {
                        break;
                    }
                    if (auto it = texts_.find(item); it != texts_.end()) {
                        defaultText_ = it->second;
                    }
                }
                if (defaultText_.empty()) {
                    defaultText_ = texts_.begin()->second;
                }
                m_texts = std::move(texts_);
            }
            m_defaultText = std::move(defaultText_);
        }
    };

    DisplayText::DisplayText() : _impl(std::make_shared<Impl>()) {
    }

    DisplayText::DisplayText(std::string text) : _impl(std::make_shared<Impl>()) {
        stdc_impl_t;
        impl.m_defaultText = std::move(text);
    }

    DisplayText::DisplayText(std::string defaultText, const std::map<std::string, std::string> &texts)
        : DisplayText(std::move(defaultText)) {
        stdc_impl_t;
        impl.m_texts = {texts.begin(), texts.end()};
    }

    DisplayText::DisplayText(const srt::core::JsonValue &value) : _impl(std::make_shared<Impl>()) {
        stdc_impl_t;
        impl.assign(value);
    }

    DisplayText::~DisplayText() = default;

    DisplayText &DisplayText::operator=(std::string text) {
        stdc_impl_t;
        impl.m_defaultText = std::move(text);
        return *this;
    }

    DisplayText &DisplayText::operator=(const srt::core::JsonValue &value) {
        stdc_impl_t;
        impl.assign(value);
        return *this;
    }

    std::string DisplayText::text() const {
        stdc_impl_t;

        // 本地化查找尚未实现，当前始终返回 defaultText
        return impl.m_defaultText;
    }

    std::string DisplayText::text(const std::string_view locale) const {
        stdc_impl_t;
        if (!impl.m_texts) {
            return impl.m_defaultText;
        }
        const auto it = impl.m_texts->find(locale);
        if (it == impl.m_texts->end()) {
            return impl.m_defaultText;
        }
        return it->second;
    }

    const std::string &DisplayText::defaultText() const {
        stdc_impl_t;
        return impl.m_defaultText;
    }

    void DisplayText::set(std::string_view locale, std::string text) {
        stdc_impl_t;
        if (!impl.m_texts) {
            impl.m_texts.emplace();
        }
        impl.m_texts->insert_or_assign(std::string(locale), std::move(text));
    }

    bool DisplayText::isEmpty() const {
        stdc_impl_t;
        return impl.m_defaultText.empty();
    }

} // namespace srt::g2p
