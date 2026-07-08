#ifndef SRT_G2P_PLUGINS_CANTONESEG2PTASK_H
#define SRT_G2P_PLUGINS_CANTONESEG2PTASK_H

#include <synthrt/Core/Module/Module.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/VersionedTaskManager.h>
#include <memory>

namespace srt::g2p::plugins::CantoneseG2p
{
    class CantoneseG2pTask : public srt::g2p::Task {
    public:
        explicit CantoneseG2pTask(const srt::g2p::ModuleSpec *spec);
        ~CantoneseG2pTask() override;

        int apiLevel() const override;

        srt::core::Expected<void> initialize() override;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override;

        std::string getConfig() const override;

    private:
        srt::g2p::VersionedTaskManager _manager;
    };

} // namespace srt::g2p::plugins::CantoneseG2p

#endif // SRT_G2P_PLUGINS_CANTONESEG2PTASK_H
