#pragma once

#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/VersionedTaskManager.h>
#include <synthrt/Core/Module/Module.h>
#include <memory>

namespace srt::g2p::plugins::MandarinG2p {

    class MandarinG2pTask : public srt::g2p::Task {
    public:
        explicit MandarinG2pTask(const srt::g2p::ModuleSpec *spec);
        ~MandarinG2pTask() override;

        int apiLevel() const override;

        srt::core::Expected<void> initialize() override;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override;

        std::string getConfig() const override;

    private:
        srt::g2p::VersionedTaskManager m_manager;
    };

}
