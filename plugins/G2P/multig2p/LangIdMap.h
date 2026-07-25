#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

#include <synthrt/Core/Support/JSON.h>

namespace srt::g2p::plugins::Multig2p {

    /// lang_ref → lang_id 映射，由 bundle.json.languages 列表索引构造。
    ///
    /// 构造规则（docs/stable/02-synthrt-plugin.md §3.2）：
    ///   lang_id_map[ref] = idx, ref = bundle.json.languages[idx]
    ///
    /// 查询时调用方传入 synthrt languageId（如 "eng" / "eng_plus"），
    /// 通过 normalizeLanguageId 转 lang_ref（"eng/default" / "eng/plus"）后查询。
    class LangIdMap {
    public:
        LangIdMap() = default;

        /// 从 bundle.json.languages 构造映射表。
        static LangIdMap fromLanguages(const std::vector<std::string> &languages);

        /// 从 JSON 数组构造（languages 字段值）。
        static LangIdMap fromJson(const srt::core::JsonValue &languagesJson);

        /// 查询 lang_ref（如 "eng/default"）→ lang_id；缺失返回 -1。
        int lookup(const std::string &langRef) const;

        /// 将 synthrt languageId（如 "eng" / "eng_plus"）转为 lang_ref。
        /// - 无下划线：languageId + "/default"（如 "eng" → "eng/default"）
        /// - 含下划线：拆分后 lang + "/" + variant（如 "eng_plus" → "eng/plus"）
        static std::string normalizeLanguageId(const std::string &languageId);

        /// 返回全部 lang_ref 列表（与 bundle.json.languages 一致）。
        const std::vector<std::string> &languages() const { return m_languages; }

    private:
        std::vector<std::string> m_languages;
        std::unordered_map<std::string, int> m_refToId;
    };

}
