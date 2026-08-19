#include "FormatStep.h"
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
    std::string FormatStep::applyCleaner(const std::string &lyric,
                                         const std::vector<std::string> &operations)
    {
        std::string result = lyric;
        for (const auto &op : operations) {
            if (op == "lowercase") {
                std::transform(result.begin(), result.end(), result.begin(),
                               [](unsigned char c) { return static_cast<unsigned char>(std::tolower(c)); });
            }
        }
        return result;
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
