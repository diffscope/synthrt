#ifndef SRT_G2P_PLUGINS_CANTONESEG2P_INTERNAL_V1_TASKIMPL_H
#define SRT_G2P_PLUGINS_CANTONESEG2P_INTERNAL_V1_TASKIMPL_H

#include <PinyinG2pTaskImplBase.h>
#include <memory>

#include <cpp-pinyin/Jyutping.h>

namespace srt::g2p::plugins::CantoneseG2p::Internal::V1
{
    class CantoneseG2pTaskImpl final : public srt::g2p::plugins::Common::PinyinG2pTaskImplBase {
    public:
        explicit CantoneseG2pTaskImpl(const srt::g2p::ModuleSpec *spec);
        ~CantoneseG2pTaskImpl() override = default;

    protected:
        srt::core::Expected<void> onInitializeEngine() override;
        bool isEngineInitialized() const override;
        std::vector<Pinyin::PinyinRes> doHanziToPinyin(const std::vector<std::string> &input) override;

    private:
        std::unique_ptr<Pinyin::Jyutping> m_engine;
    };

} // namespace srt::g2p::plugins::CantoneseG2p::Internal::V1

#endif // SRT_G2P_PLUGINS_CANTONESEG2P_INTERNAL_V1_TASKIMPL_H
