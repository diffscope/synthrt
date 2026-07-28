#pragma once

#include "../Core/G2pStep.h"
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/G2pTask.h>
#include <memory>

namespace srt::g2p::plugins::ChainG2p {
    /// ModelStep - 模型推理步骤
    ///
    /// 使用 AI 模型生成发音
    class ModelStep : public G2pStep {
    public:
        ModelStep() = default;
        ~ModelStep() override = default;

        srt::core::Expected<void> configure(const srt::g2p::ModuleSpec *spec,
                                            const srt::core::JsonObject &config) override;

        void handle(G2pContext &context) override;

        std::string name() const override { return "model"; }

        void cleanup() override;

    private:
        // T7: centralized batch size default.
        static constexpr int kModelStepBatchSize = 50;

        bool m_enabled = true;
        std::string m_onnxG2pId;
        int m_batchSize = kModelStepBatchSize;
        std::string m_langRef;  // optional lang_ref override passed to the model
        srt::core::NO<srt::g2p::Task> m_onnxTask;

        void processBatch(G2pContext &context,
                         const std::vector<size_t> &indices,
                         const std::vector<std::string> &words);
    };

}
