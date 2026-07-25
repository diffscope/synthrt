#include "TaskImpl.h"

#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/Pinyin.h>
#include <cpp-pinyin/ManTone.h>

namespace srt::g2p::plugins::MandarinG2p::Internal::V1 {

    MandarinG2pTaskImpl::MandarinG2pTaskImpl(const srt::g2p::ModuleSpec *spec)
        : PinyinG2pTaskImplBase(spec, {"dictPath", "Mandarin"}) {}

    srt::core::Expected<void> MandarinG2pTaskImpl::onInitializeEngine() {
        m_engine = std::make_unique<Pinyin::Pinyin>();
        return {};
    }

    bool MandarinG2pTaskImpl::isEngineInitialized() const {
        return m_engine && m_engine->initialized();
    }

    std::vector<Pinyin::PinyinRes> MandarinG2pTaskImpl::doHanziToPinyin(const std::vector<std::string> &input) {
        return m_engine->hanziToPinyin(input, Pinyin::ManTone::NORMAL, Pinyin::Default, true, false, false);
    }

}
