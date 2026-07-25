#pragma once

#include "../Core/G2pStep.h"

namespace srt::g2p::plugins::ChainG2p {
    /// FallbackStep - 回退步骤
    ///
    /// 处理转换失败的词
    class FallbackStep : public G2pStep {
    public:
        FallbackStep() = default;
        ~FallbackStep() override = default;

        srt::core::Expected<void> configure(const srt::g2p::ModuleSpec *spec,
                                            const srt::core::JsonObject &config) override;

        void handle(G2pContext &context) override;

        std::string name() const override { return "fallback"; }

    private:
        bool m_useOriginal = true;
        std::string m_defaultPronunciation;
    };

}
