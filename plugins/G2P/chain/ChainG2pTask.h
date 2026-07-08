#ifndef SRT_G2P_PLUGINS_CHAING2PTASK_H
#define SRT_G2P_PLUGINS_CHAING2PTASK_H

#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/VersionedTaskManager.h>
#include <synthrt/Core/Module/Module.h>
#include <memory>

namespace srt::g2p::plugins::ChainG2p
{
    class ChainG2pTask : public srt::g2p::Task {
    public:
        explicit ChainG2pTask(const srt::g2p::ModuleSpec *spec);
        ~ChainG2pTask() override;

        int apiLevel() const override;

        srt::core::Expected<void> initialize() override;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override;

        std::string getConfig() const override;

    private:
        srt::g2p::VersionedTaskManager _manager;
    };

} // namespace srt::g2p::plugins::ChainG2p

#endif // SRT_G2P_PLUGINS_CHAING2PTASK_H
