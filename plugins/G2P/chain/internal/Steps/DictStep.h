#ifndef SRT_G2P_PLUGINS_CHAING2P_STEPS_DICTSTEP_H
#define SRT_G2P_PLUGINS_CHAING2P_STEPS_DICTSTEP_H

#include "../Core/G2pStep.h"
#include <synthrt/G2P/Support/PhonemeDict.h>
#include <memory>

namespace srt::g2p::plugins::ChainG2p
{
    /// DictStep - 字典查找步骤
    ///
    /// 从字典中查找发音
    class DictStep : public G2pStep {
    public:
        DictStep() = default;
        ~DictStep() override = default;

        srt::core::Expected<void> configure(const srt::g2p::ModuleSpec *spec,
                                            const srt::core::JsonObject &config) override;

        void handle(G2pContext &context) override;

        std::string name() const override { return "dict"; }

        void cleanup() override;

    private:
        bool m_enabled = true;
        std::filesystem::path m_dictPath;
        srt::g2p::PhonemeDict m_phonemeDict;

        std::vector<std::string> lookup(const std::string &key) const;
    };

} // namespace srt::g2p::plugins::ChainG2p

#endif // SRT_G2P_PLUGINS_CHAING2P_STEPS_DICTSTEP_H
