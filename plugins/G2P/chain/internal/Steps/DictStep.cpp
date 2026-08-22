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

        // 把一条发音（音素序列视图）拼成空格分隔的整串，不含尾随空格，避免
        // 下游 S2P DirectS2P::convert 切分出空 token 导致查表失败。
        static const auto joinPronunciation = [](const srt::g2p::PhonemeList &phonemes) {
            std::string pronStr;
            bool        first = true;
            for (const char *phone : phonemes) {
                if (!first) {
                    pronStr += ' ';
                }
                pronStr += phone;
                first = false;
            }
            return pronStr;
        };

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

            // 查找字典：多变体词（CMU 式 "(n)" 后缀已在 PhonemeDict 加载时
            // 归并）返回全部发音，按文件序排列，裸 base 发音在首位。
            const auto variants = m_phonemeDict.lookupAll(lookupKey.c_str());
            if (!variants.empty()) {
                // pronunciation 取第一个变体（= base，与历史行为一致）；
                // candidates 每项是一个完整发音整串（与 ModelStep/G2pRes 的
                // 候选语义一致），多音词的全部读法均提供给编辑器。
                word.pronunciation = joinPronunciation(variants.front());

                word.candidates.clear();
                word.candidates.reserve(variants.size());
                for (const auto &variant : variants) {
                    word.candidates.push_back(joinPronunciation(variant));
                }

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
        // 向后兼容的单查接口：返回第一个变体的音素序列（多变体归并后与
        // 历史 find() 行为一致，均命中 base 行）。全部发音请用
        // PhonemeDict::lookupAll。
        std::vector<std::string> result;

        const auto variants = m_phonemeDict.lookupAll(key.c_str());
        if (!variants.empty()) {
            // PhonemeList does not have size(), but we can use vec() to convert to vector
            auto phonemeVec = variants.front().vec();
            result.reserve(phonemeVec.size());
            for (const auto &phone : phonemeVec) {
                result.emplace_back(phone);
            }
        }

        return result;
    }

}
