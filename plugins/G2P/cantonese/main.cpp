#include <synthrt/G2P/Task/TaskPlugin.h>
#include "CantoneseG2pTask.h"

using namespace srt::g2p;
using namespace srt::g2p::plugins::CantoneseG2p;

class CantoneseG2pEnginePlugin final : public srt::g2p::TaskPlugin {
public:
    int apiLevel() const override { return 1; }
    const char *key() const override { return "g2p.template.CantoneseG2pInference"; }
    srt::core::Expected<srt::core::NO<srt::g2p::Task>> createTask(
        const srt::g2p::ModuleSpec *spec) override {
        return srt::core::NO<CantoneseG2pTask>::create(spec);
    }
};
SRT_EXPORT_PLUGIN(CantoneseG2pEnginePlugin)
