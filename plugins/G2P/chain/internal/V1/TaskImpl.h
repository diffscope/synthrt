#pragma once

#include <synthrt/G2P/Task/VersionedTaskImplBase.h>
#include <memory>
#include <shared_mutex>
#include <string>

#include <synthrt/Core/Module/Module.h>
#include "../Core/G2pPipeline.h"

namespace srt::g2p::plugins::ChainG2p::Internal::V1 {
    /// ChainG2pTaskImpl - ChainG2p 的 Level 1 实现
    /// 使用责任链模式处理 G2p 转换
    class ChainG2pTaskImpl final : public srt::g2p::VersionedTaskImplBase {
    public:
        explicit ChainG2pTaskImpl(const srt::g2p::ModuleSpec *spec);
        ~ChainG2pTaskImpl() override = default;

        srt::core::Expected<void> initialize() override;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override;

        std::string getConfig() const override;

        /// 设置 Task 对象
        void setTask(srt::g2p::Task* task) { m_task = task; }

        /// 获取 Task 对象
        srt::g2p::Task* task() const { return m_task; }

    private:
        const srt::g2p::ModuleSpec* m_spec;
        srt::g2p::Task* m_task = nullptr;
        std::unique_ptr<G2pPipeline> m_pipeline;
        mutable std::shared_mutex m_mutex;
    };

}
