#include "TagAndValidateStep.h"
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/Support/PhonemeDict.h>
#include <re2/re2.h>

namespace srt::g2p::plugins::ChainG2p
{
    srt::core::Expected<void> TagAndValidateStep::configure(const srt::g2p::ModuleSpec *spec,
                                                            const srt::core::JsonObject &config)
    {
        m_spec = spec;

        // 解析配置
        std::vector<VerifyEntry> entries;
        
        // 查找 tagger 字段
        auto taggerIt = config.find("tagger");
        if (taggerIt != config.end() && taggerIt->second.isArray()) {
            const auto &taggerArray = taggerIt->second.toArray();
            
            for (const auto &item : taggerArray) {
                if (!item.isObject()) {
                    continue;
                }
                
                const auto &obj = item.toObject();
                VerifyEntry entry;
                
                // 解析 type
                auto typeIt = obj.find("type");
                if (typeIt != obj.end() && typeIt->second.isString()) {
                    entry.type = typeIt->second.toString();
                }
                
                // 解析 value
                auto valueIt = obj.find("value");
                if (valueIt != obj.end() && valueIt->second.isArray()) {
                    for (const auto &v : valueIt->second.toArray()) {
                        if (v.isString()) {
                            entry.value.push_back(v.toString());
                        }
                    }
                }
                
                // 解析 action
                auto actionIt = obj.find("action");
                if (actionIt != obj.end() && actionIt->second.isString()) {
                    entry.mode = actionIt->second.toString();
                }
                
                entries.push_back(entry);
            }
        } else {
            // 如果没有配置 tagger，使用默认规则
            m_verifyEntries = {
                {"regex", {"([A-Z]+)"}, srt::g2p::kG2pModeCopy},
                {"regex", {"([a-z]+)"}, srt::g2p::kG2pModeConvert}
            };
            compileEntries();
            return {};
        }
        
        m_verifyEntries = entries;
        
        // 预编译所有正则
        compileEntries();
        return {};
    }

    void TagAndValidateStep::compileEntries() {
        m_compiledEntries.clear();
        RE2::Options options;
        options.set_encoding(RE2::Options::EncodingUTF8);

        for (const auto &entry : m_verifyEntries) {
            if (entry.type == "regex") {
                // 合并多个模式
                std::string merged;
                for (size_t i = 0; i < entry.value.size(); ++i) {
                    if (i > 0)
                        merged += "|";
                    merged += entry.value[i];
                }
                CompiledVerifyEntry compiled;
                compiled.regex = std::make_unique<RE2>(merged, options);
                compiled.mode = entry.mode;
                m_compiledEntries.push_back(std::move(compiled));
            }
        }
    }

    void TagAndValidateStep::handle(G2pContext &context)
    {
        for (auto &word : context.words()) {
            // 优先使用 cleanedLyric，如果为空则使用 lyric
            std::string wordToCheck = word.cleanedLyric.empty() ? word.lyric : word.cleanedLyric;
            std::string mode;
            if (verifyWord(wordToCheck, mode)) {
                word.mode = mode;
            } else {
                word.mode = srt::g2p::kG2pModeCopy;  // 默认模式
            }
        }
    }

    bool TagAndValidateStep::verifyWord(const std::string &word, std::string &mode) const
    {
        for (const auto &entry : m_compiledEntries) {
            if (entry.regex && entry.regex->ok() && RE2::FullMatch(word, *entry.regex)) {
                mode = entry.mode;
                return true;
            }
        }

        return false;
    }

} // namespace srt::g2p::plugins::ChainG2p
