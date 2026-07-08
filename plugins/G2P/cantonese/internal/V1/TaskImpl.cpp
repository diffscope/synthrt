#include "TaskImpl.h"

#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/Jyutping.h>
#include <cpp-pinyin/CanTone.h>

namespace srt::g2p::plugins::CantoneseG2p::Internal::V1
{

    CantoneseG2pTaskImpl::CantoneseG2pTaskImpl(const srt::g2p::ModuleSpec *spec)
        : PinyinG2pTaskImplBase(spec, {"dictPath", "Cantonese"}) {}

    srt::core::Expected<void> CantoneseG2pTaskImpl::onInitializeEngine() {
        m_engine = std::make_unique<Pinyin::Jyutping>();
        return {};
    }

    bool CantoneseG2pTaskImpl::isEngineInitialized() const {
        return m_engine && m_engine->initialized();
    }

    std::vector<Pinyin::PinyinRes> CantoneseG2pTaskImpl::doHanziToPinyin(const std::vector<std::string> &input) {
        return m_engine->hanziToPinyin(input, Pinyin::CanTone::NORMAL, Pinyin::Default, true);
    }

} // namespace srt::g2p::plugins::CantoneseG2p::Internal::V1
