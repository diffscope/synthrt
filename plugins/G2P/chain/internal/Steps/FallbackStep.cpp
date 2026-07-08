#include "FallbackStep.h"
#include <synthrt/Core/Support/ConfigAccessor.h>

namespace srt::g2p::plugins::ChainG2p
{
    srt::core::Expected<void> FallbackStep::configure(const srt::g2p::ModuleSpec *spec,
                                                      const srt::core::JsonObject &config)
    {
        m_spec = spec;

        auto useOriginalIt = config.find("useOriginal");
        m_useOriginal = (useOriginalIt != config.end() && useOriginalIt->second.isBool()) ? useOriginalIt->second.toBool() : true;

        auto defaultPronIt = config.find("defaultPronunciation");
        if (defaultPronIt != config.end() && defaultPronIt->second.isString()) {
            m_defaultPronunciation = defaultPronIt->second.toString();
        }

        return {};
    }

    void FallbackStep::handle(G2pContext &context)
    {
        for (auto &word : context.words()) {
            // 跳过已丢弃或已完成的词
            if (word.discard || word.mode == srt::g2p::kG2pModeCopy) {
                continue;
            }

            // 如果发音为空，使用回退策略
            if (word.pronunciation.empty()) {
                if (m_useOriginal) {
                    word.pronunciation = word.lyric;
                    word.candidates = {word.lyric};
                } else if (!m_defaultPronunciation.empty()) {
                    word.pronunciation = m_defaultPronunciation;
                    word.candidates = {m_defaultPronunciation};
                }

                word.fromFallback = true;
                word.errorType = srt::g2p::PhonemeGenerationFailed;
            }
        }
    }

} // namespace srt::g2p::plugins::ChainG2p
