#include "FormatStep.h"
#include <cctype>

namespace srt::g2p::plugins::ChainG2p
{
    srt::core::Expected<void> FormatStep::configure(const srt::g2p::ModuleSpec *spec,
                                                    const srt::core::JsonObject &config)
    {
        auto stripIt = config.find("stripTrailingSpace");
        m_stripTrailingSpace = (stripIt != config.end() && stripIt->second.isBool()) ? stripIt->second.toBool() : false;

        auto tonesIt = config.find("normalizeTones");
        m_normalizeTones = (tonesIt != config.end() && tonesIt->second.isBool()) ? tonesIt->second.toBool() : false;

        auto spaceIt = config.find("addSpaceBetweenPhones");
        m_addSpaceBetweenPhones = (spaceIt != config.end() && spaceIt->second.isBool()) ? spaceIt->second.toBool() : false;

        return {};
    }

    void FormatStep::handle(G2pContext &context)
    {
        for (auto &word : context.words()) {
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

} // namespace srt::g2p::plugins::ChainG2p
