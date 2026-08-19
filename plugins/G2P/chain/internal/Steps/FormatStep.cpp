#include "FormatStep.h"
#include <stdcorelib/utf.h>
#include <algorithm>
#include <cctype>

namespace srt::g2p::plugins::ChainG2p {
    srt::core::Expected<void> FormatStep::configure(const srt::g2p::ModuleSpec *spec,
                                                    const srt::core::JsonObject &config)
    {
        auto stripIt = config.find("stripTrailingSpace");
        m_stripTrailingSpace = (stripIt != config.end() && stripIt->second.isBool()) ? stripIt->second.toBool() : false;

        auto tonesIt = config.find("normalizeTones");
        m_normalizeTones = (tonesIt != config.end() && tonesIt->second.isBool()) ? tonesIt->second.toBool() : false;

        auto spaceIt = config.find("addSpaceBetweenPhones");
        m_addSpaceBetweenPhones = (spaceIt != config.end() && spaceIt->second.isBool()) ? spaceIt->second.toBool() : false;

        // 解析 cleaner 配置（形状与 Cleaner-Eng 一致）：
        //   "cleaner": { "operations": ["lowercase"] }
        auto cleanerIt = config.find("cleaner");
        if (cleanerIt != config.end() && cleanerIt->second.isObject()) {
            const auto &cleanerObj = cleanerIt->second.toObject();
            auto opsIt = cleanerObj.find("operations");
            if (opsIt != cleanerObj.end() && opsIt->second.isArray()) {
                for (const auto &op : opsIt->second.toArray()) {
                    if (op.isString()) {
                        m_cleanerOperations.push_back(op.toString());
                    }
                }
            }
        }

        return {};
    }

    /// cleaner 操作实现：目前支持 "lowercase"，其余操作忽略。
    /// 清洗产生的任何歌词修改都集中在此（FormatStep 内），
    /// 不在 ds-dict / multig2p / 其他步骤中做大小写转换。
    ///
    /// lowercase 为 UTF-8 感知：对每个 Unicode 码点做小写映射。
    /// 仅处理各 ChainG2p 既有语言脚本的常见大写 → 小写区间（拉丁字母、
    /// 拉丁文增补 1、拉丁文扩展-A、西里尔文）；C 语言环境的 std::tolower
    /// 只覆盖 ASCII，无法处理西里尔文（俄语）等非 ASCII 大写字母。
    std::string FormatStep::applyCleaner(const std::string &lyric,
                                         const std::vector<std::string> &operations)
    {
        std::string result = lyric;
        for (const auto &op : operations) {
            if (op == "lowercase") {
                result = toLowercaseUtf8(result);
            }
        }
        return result;
    }

    /// UTF-8 小写化：UTF-8 → UTF-32（每个码点一个 char32_t），按码点
    /// 映射大写 → 小写，再转回 UTF-8。未覆盖的码点保持不变。
    std::string FormatStep::toLowercaseUtf8(const std::string &lyric)
    {
        auto u32 = stdc::utf::utf8_to_utf32(lyric, stdc::utf::error_policy::replace);
        for (auto &cp : u32) {
            cp = lowercaseCodePoint(cp);
        }
        return stdc::utf::utf32_to_utf8(u32, stdc::utf::error_policy::replace);
    }

    /// 单个码点的小写映射。覆盖:
    ///   ASCII 大写 (A-Z)
    ///   拉丁文增补 1 (À-Þ, 0xC0-0xDE, 排除 0xD7 ×)
    ///   拉丁文扩展-A (每个偶数码点 → +1, 0x0100-0x017E)
    ///   西里尔文 (А-Я 0x0410-0x042F → +0x20; Ѐ-Џ 0x0400-0x040F → +0x50)
    char32_t FormatStep::lowercaseCodePoint(char32_t cp)
    {
        if (cp >= 0x41 && cp <= 0x5A) {           // A-Z
            return cp + 0x20;
        }
        if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) { // À-Þ (Latin-1, × excluded)
            return cp + 0x20;
        }
        if (cp >= 0x0100 && cp <= 0x017E && (cp % 2) == 0) { // Latin Extended-A
            if (cp != 0x0130) {                   // İ special
                return cp + 1;
            }
        }
        if (cp >= 0x0410 && cp <= 0x042F) {       // Cyrillic А-Я
            return cp + 0x20;
        }
        if (cp >= 0x0400 && cp <= 0x040F) {       // Cyrillic Ѐ-Џ
            return cp + 0x50;
        }
        return cp;
    }

    void FormatStep::handle(G2pContext &context)
    {
        for (auto &word : context.words()) {
            // 歌词清洗：只对仍需转换（尚未获得发音）的词执行 cleaner 操作，
            // 结果写入 cleanedLyric；word.lyric 保持原样。
            if (word.mode == srt::g2p::kG2pModeConvert && !word.discard &&
                word.pronunciation.empty() && !m_cleanerOperations.empty()) {
                word.cleanedLyric = applyCleaner(word.lyric, m_cleanerOperations);
            }

            if (word.pronunciation.empty()) {
                continue;
            }

            std::string formatted = word.pronunciation;

            // 应用格式化规则
            if (m_stripTrailingSpace) {
                formatted = stripTrailingSpace(formatted);
            }

            if (m_addSpaceBetweenPhones) {
                formatted = addSpaceAtAlnumBoundary(formatted);
            }

            word.pronunciation = formatted;
        }
    }

    std::string FormatStep::stripTrailingSpace(const std::string &str)
    {
        size_t end = str.find_last_not_of(" \t\n\r");
        if (end == std::string::npos) {
            return "";
        }
        return str.substr(0, end + 1);
    }

    std::string FormatStep::addSpaceAtAlnumBoundary(const std::string &str)
    {
        std::string result;
        bool lastWasPhone = false;

        for (char c : str) {
            if (std::isalnum(c)) {
                result += c;
                lastWasPhone = true;
            } else if (lastWasPhone && c != ' ') {
                result += ' ';
                result += c;
                lastWasPhone = false;
            } else {
                result += c;
                lastWasPhone = false;
            }
        }

        return result;
    }

}
