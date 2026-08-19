#include "DictStep.h"
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/G2P/Support/Error.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <algorithm>
#include <cctype>

namespace srt::g2p::plugins::ChainG2p {
    srt::core::Expected<void> DictStep::configure(const srt::g2p::ModuleSpec *spec,
                                                   const srt::core::JsonObject &config)
    {
        m_spec = spec;

        // 解析 enabled
        auto enabledIt = config.find("enabled");
        if (enabledIt != config.end() && enabledIt->second.isBool()) {
            m_enabled = enabledIt->second.toBool();
        } else {
            m_enabled = true;
        }

        if (!m_enabled) {
            return {};
        }

        // 解析 dictPath - 使用 ConfigAccessor 统一路径规范化
        auto cfg = srt::core::ConfigAccessor(config, spec->path());
        auto dictPathExp = cfg.getResolvedPath("file");
        if (!dictPathExp) {
            return dictPathExp.takeError();
        }
        m_dictPath = dictPathExp.take();

        // 加载字典
        if (m_dictPath.empty()) {
            return srt::g2p::Error(srt::g2p::ErrorCode::G2pFileSystemError, "Dictionary path is empty");
        }

        std::error_code errorCode;
        if (!m_phonemeDict.load(m_dictPath, &errorCode)) {
            return srt::g2p::Error(srt::g2p::ErrorCode::G2pFileSystemError,
                                 stdc::formatN("Failed to load dictionary: %1 (%2)",
                                               stdc::path::to_utf8(m_dictPath), errorCode.value()));
        }

        return {};
    }

    void DictStep::handle(G2pContext &context)
    {
        if (!m_enabled) {
            return;
        }

        for (auto &word : context.words()) {
            // 只处理需要转换、未丢弃且尚未获得发音的词
            if (word.mode != srt::g2p::kG2pModeConvert || word.discard ||
                !word.pronunciation.empty()) {
                continue;
            }

            // 原样精确查找：优先使用 FormatStep 清洗后的词（cleanedLyric），
            // 否则使用原歌词。本步骤不做任何大小写转换（转小写由 FormatStep
            // 的 cleaner 统一负责），也不修改歌词。
            std::string lookupKey = word.cleanedLyric.empty() ? word.lyric : word.cleanedLyric;

            // 查找字典
            auto phonemes = lookup(lookupKey);
            if (!phonemes.empty()) {
                // 构建发音字符串（音素间用空格分隔，不含尾随空格，避免
                // 下游 S2P DirectS2P::convert 切分出空 token 导致查表失败）
                std::string pronStr;
                for (size_t i = 0; i < phonemes.size(); ++i) {
                    if (i > 0) {
                        pronStr += ' ';
                    }
                    pronStr += phonemes[i];
                }

                word.pronunciation = pronStr;
                word.candidates = phonemes;
                word.fromDict = true;
            }
        }
    }

    void DictStep::cleanup()
    {
        m_phonemeDict.reset();
    }

    std::vector<std::string> DictStep::lookup(const std::string &key) const
    {
        std::vector<std::string> result;

        if (const auto it = m_phonemeDict.find(key.c_str()); it != m_phonemeDict.end()) {
            const auto &phonemes = it->second;
            // PhonemeList does not have size(), but we can use vec() to convert to vector
            auto phonemeVec = phonemes.vec();
            result.reserve(phonemeVec.size());
            for (const auto &phone : phonemeVec) {
                result.emplace_back(phone);
            }
        }

        return result;
    }

}
