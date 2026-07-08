#ifndef SRT_G2P_PLUGINS_CHAING2P_STEPS_FORMATSTEP_H
#define SRT_G2P_PLUGINS_CHAING2P_STEPS_FORMATSTEP_H

#include "../Core/G2pStep.h"

namespace srt::g2p::plugins::ChainG2p
{
    /// FormatStep - 格式化步骤
    ///
    /// 格式化输出结果
    class FormatStep : public G2pStep {
    public:
        FormatStep() = default;
        ~FormatStep() override = default;

        srt::core::Expected<void> configure(const srt::g2p::ModuleSpec *spec,
                                            const srt::core::JsonObject &config) override;

        void handle(G2pContext &context) override;

        std::string name() const override { return "format"; }

    private:
        bool m_stripTrailingSpace = false;
        bool m_normalizeTones = false;
        bool m_addSpaceBetweenPhones = false;

        static std::string stripTrailingSpace(const std::string &str);
        static std::string addSpaceAtAlnumBoundary(const std::string &str);
    };

} // namespace srt::g2p::plugins::ChainG2p

#endif // SRT_G2P_PLUGINS_CHAING2P_STEPS_FORMATSTEP_H
