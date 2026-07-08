#ifndef SRT_G2P_SUPPORT_DISPLAYTEXT_H
#define SRT_G2P_SUPPORT_DISPLAYTEXT_H

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <synthrt/Core/Support/JSON.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p {

    /// DisplayText - 表示支持多语言的显示文本
    /// 设计原则：简洁可靠、无感调用、自动本地化
    ///
    /// 使用示例：
    ///   DisplayText text("Hello", {{"zh", "你好"}, {"ja", "こんにちは"}});
    ///   std::string result = text.text();  // 自动返回当前语言的文本（无感调用）
    ///   std::string zh = text.text("zh");  // 指定语言
    ///   std::string def = text.defaultText();  // 获取默认文本
    class SRT_G2P_EXPORT DisplayText {
    public:
        /// 构造空的显示文本对象
        DisplayText();

        /// 使用默认文本构造
        explicit DisplayText(std::string text);

        /// 使用默认文本和语言映射构造
        /// @param defaultText 默认文本
        /// @param texts 语言代码到文本的映射
        DisplayText(std::string defaultText, const std::map<std::string, std::string> &texts);

        /// 从 JSON 值构造
        /// @note JSON 值必须是字符串映射对象，键为语言代码，值为对应文本
        ///       如果存在 "_" 属性，则将其作为默认文本；否则搜索 "en_XX" 属性
        explicit DisplayText(const srt::core::JsonValue &value);

        ~DisplayText();

        DisplayText &operator=(std::string text);
        DisplayText &operator=(const srt::core::JsonValue &value);

        void swap(DisplayText &RHS) noexcept { _impl.swap(RHS._impl); }

        /// 获取文本（自动返回当前语言的本地化文本，无感调用）
        /// @note 本地化查找尚未实现，当前始终返回 defaultText
        /// @return 当前语言的本地化文本，如果不存在则返回默认文本
        std::string text() const;

        /// 获取指定语言的文本
        /// @param locale 语言代码（如 "en", "zh", "ja"）
        /// @return 指定语言的文本，如果不存在则返回默认文本
        std::string text(std::string_view locale) const;

        /// 获取默认文本
        /// @return 默认文本
        const std::string &defaultText() const;

        /// 设置指定语言的文本
        void set(std::string_view locale, std::string text);

        /// 检查是否为空
        bool isEmpty() const;

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

} // namespace srt::g2p

#endif // SRT_G2P_SUPPORT_DISPLAYTEXT_H
