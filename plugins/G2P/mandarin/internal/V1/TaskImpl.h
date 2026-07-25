#pragma once

#include <PinyinG2pTaskImplBase.h>
#include <memory>

#include <cpp-pinyin/Pinyin.h>

namespace srt::g2p::plugins::MandarinG2p::Internal::V1 {

    class MandarinG2pTaskImpl final : public srt::g2p::plugins::Common::PinyinG2pTaskImplBase {
    public:
        explicit MandarinG2pTaskImpl(const srt::g2p::ModuleSpec *spec);
        ~MandarinG2pTaskImpl() override = default;

    protected:
        srt::core::Expected<void> onInitializeEngine() override;
        bool isEngineInitialized() const override;
        std::vector<Pinyin::PinyinRes> doHanziToPinyin(const std::vector<std::string> &input) override;

    private:
        std::unique_ptr<Pinyin::Pinyin> m_engine;
    };

}
