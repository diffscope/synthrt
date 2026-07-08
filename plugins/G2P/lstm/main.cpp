#include <synthrt/G2P/Task/TaskPlugin.h>
#include "LstmG2pTask.h"

using namespace srt::g2p;
using namespace srt::g2p::plugins::LstmG2p;

class LstmG2pEnginePlugin final : public srt::g2p::TaskPlugin {
public:
    int apiLevel() const override { return 1; }
    const char *key() const override { return "g2p.model.LstmG2pInference"; }
    srt::core::Expected<srt::core::NO<srt::g2p::Task>> createTask(
        const srt::g2p::ModuleSpec *spec) override {
        return srt::core::NO<LstmG2pTask>::create(spec);
    }
};
SRT_EXPORT_PLUGIN(LstmG2pEnginePlugin)
