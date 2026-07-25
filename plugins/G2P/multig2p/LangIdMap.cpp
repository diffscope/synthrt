#include "LangIdMap.h"

namespace srt::g2p::plugins::Multig2p {

    LangIdMap LangIdMap::fromLanguages(const std::vector<std::string> &languages) {
        LangIdMap m;
        m.m_languages = languages;
        for (size_t i = 0; i < languages.size(); ++i) {
            m.m_refToId[languages[i]] = static_cast<int>(i);
        }
        return m;
    }

    LangIdMap LangIdMap::fromJson(const srt::core::JsonValue &languagesJson) {
        std::vector<std::string> langs;
        if (!languagesJson.isArray()) {
            return LangIdMap{};
        }
        const auto &arr = languagesJson.toArray();
        langs.reserve(arr.size());
        for (const auto &v : arr) {
            if (v.isString()) {
                langs.push_back(v.toString());
            }
        }
        return fromLanguages(langs);
    }

    int LangIdMap::lookup(const std::string &langRef) const {
        const auto it = m_refToId.find(langRef);
        if (it == m_refToId.end()) {
            return -1;
        }
        return it->second;
    }

    std::string LangIdMap::normalizeLanguageId(const std::string &languageId) {
        const auto pos = languageId.find('_');
        if (pos == std::string::npos) {
            return languageId + "/default";
        }
        return languageId.substr(0, pos) + "/" + languageId.substr(pos + 1);
    }

}
