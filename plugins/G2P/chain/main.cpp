#include <synthrt/G2P/Task/TaskPlugin.h>

#include "ChainG2pTask.h"

namespace srt::g2p::plugins::ChainG2p {

    class ChainG2pPlugin final : public srt::g2p::TaskPlugin {
    public:
        int apiLevel() const override { return 1; }
        const char *key() const override { return "g2p.chain.ChainG2pInference"; }
        srt::core::Expected<srt::core::NO<srt::g2p::Task>> createTask(
            const srt::g2p::ModuleSpec *spec) override {
            return srt::core::NO<ChainG2pTask>::create(spec);
        }
    };

}

SRT_EXPORT_PLUGIN(srt::g2p::plugins::ChainG2p::ChainG2pPlugin)
