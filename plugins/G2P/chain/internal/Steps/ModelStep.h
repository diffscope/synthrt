#pragma once

#include <synthrt/G2P/Task/G2pTask.h>
#include <synthrt/G2P/Task/Task.h>

#include <atomic>
#include <memory>

#include "../Core/G2pStep.h"

namespace srt::g2p::plugins::ChainG2p {
    /// ModelStep - 模型推理步骤
    ///
    /// 使用 AI 模型生成发音。未命中词典的词（含带连字符的词）作为整体
    /// 送入模型推理，与其他流程一致。
    ///
    /// 模型任务（如 g2p-multig2p-multi-official）按需在每次 handle() 时
    /// 解析：插件间依赖在初始化阶段已解决，但在 configure() 时枚举任务
    /// 可能过早（依赖插件尚未注册），因此不在 configure 阶段解析/报警；
    /// 仅在实际需要推理且任务仍不可用时才告警并回退原词。
    class ModelStep : public G2pStep {
    public:
        ModelStep()           = default;
        ~ModelStep() override = default;

        srt::core::Expected<void> configure(const srt::g2p::ModuleSpec  *spec,
                                            const srt::core::JsonObject &config) override;

        void handle(G2pContext &context) override;

        std::string name() const override {
            return "model";
        }

    private:
        // T7: centralized batch size default.
        static constexpr int kModelStepBatchSize = 50;

        bool                          m_enabled = true;
        std::string                   m_onnxG2pId;
        int                           m_batchSize = kModelStepBatchSize;
        std::string                   m_langRef; // optional lang_ref override passed to the model
        std::atomic<bool>             m_taskWarned{false};

        /// 解析模型任务：本 context 优先，找不到且非默认 context 时回退默认 context。
        srt::core::NO<srt::g2p::Task> resolveTask() const;

        /// 使用模型推理一批单词，结果按 flat 顺序写入 prons/cands/errors（与 words 对齐）。
        void processBatch(const srt::core::NO<srt::g2p::Task> &task,
                          const std::vector<std::string> &words, std::vector<std::string> &prons,
                          std::vector<std::vector<std::string>> &cands, std::vector<srt::g2p::G2pErrorType> &errors);
    };

} // namespace srt::g2p::plugins::ChainG2p
