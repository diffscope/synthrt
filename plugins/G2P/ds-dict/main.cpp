#include <synthrt/G2P/Task/TaskPlugin.h>
#include "DsDictTask.h"

using namespace srt::g2p;
using namespace srt::g2p::plugins::DsDict;

class DsDictPlugin final : public srt::g2p::TaskPlugin {
public:
    int apiLevel() const override { return 1; }
    const char *key() const override { return "dict.dsdict"; }
    srt::core::Expected<srt::core::NO<srt::g2p::Task>> createTask(
        const srt::g2p::ModuleSpec *spec) override {
        return srt::core::NO<DsDictTask>::create(spec);
    }
};
SRT_EXPORT_PLUGIN(DsDictPlugin)
