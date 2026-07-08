#ifndef SRT_G2P_PLUGINS_DSDICTTASK_H
#define SRT_G2P_PLUGINS_DSDICTTASK_H

#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/VersionedTaskManager.h>
#include <synthrt/Core/Module/Module.h>
#include <memory>

namespace srt::g2p::plugins::DsDict
{
    class DsDictTask : public srt::g2p::Task {
    public:
        explicit DsDictTask(const srt::g2p::ModuleSpec *spec);
        ~DsDictTask() override;

        int apiLevel() const override;

        srt::core::Expected<void> initialize() override;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override;

        std::string getConfig() const override;

    private:
        srt::g2p::VersionedTaskManager _manager;
    };

} // namespace srt::g2p::plugins::DsDict

#endif // SRT_G2P_PLUGINS_DSDICTTASK_H
