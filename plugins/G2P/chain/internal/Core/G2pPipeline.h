#pragma once

#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include "G2pContext.h"
#include "G2pStep.h"
#include <memory>
#include <vector>

namespace srt::g2p::plugins::ChainG2p {
    /// G2pPipeline - G2p 处理管道
    ///
    /// 负责管理多个处理步骤的执行顺序
    class G2pPipeline {
    public:
        explicit G2pPipeline(const srt::g2p::ModuleSpec *spec, srt::g2p::Task* task = nullptr)
            : m_spec(spec), m_task(task) {}
        ~G2pPipeline() = default;

        /// 配置管道
        /// @param config 配置对象
        /// @return 成功返回 Expected<void>::success()，失败返回错误信息
        srt::core::Expected<void> configure(const srt::core::JsonObject &config);

        /// 处理输入
        /// @param context 处理上下文
        void process(G2pContext &context);

        /// 清理资源
        void cleanup();

    private:
        const srt::g2p::ModuleSpec* m_spec;
        srt::g2p::Task* m_task;
        std::vector<std::shared_ptr<G2pStep>> m_steps;
    };

}
