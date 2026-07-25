#include <synthrt/G2P/Task/TaskPlugin.h>
#include <synthrt/G2P/Task/G2pTask.h>

#include "Multig2pTask.h"

namespace srt::g2p::plugins::Multig2p {

    class Multig2pPlugin final : public srt::g2p::TaskPlugin {
    public:
        int apiLevel() const override { return 1; }
        const char *key() const override { return "g2p.model.Multig2pInference"; }
        srt::core::Expected<srt::core::NO<srt::g2p::Task>> createTask(
            const srt::g2p::ModuleSpec *spec) override {
            return srt::core::NO<Multig2pTask>::create(spec);
        }
    };

}

SRT_EXPORT_PLUGIN(srt::g2p::plugins::Multig2p::Multig2pPlugin)
