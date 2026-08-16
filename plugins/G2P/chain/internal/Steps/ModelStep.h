#pragma once

#include <synthrt/G2P/Support/PhonemeDict.h>
#include <synthrt/G2P/Task/G2pTask.h>
#include <synthrt/G2P/Task/Task.h>

#include <memory>

#include "../Core/G2pStep.h"

namespace srt::g2p::plugins::ChainG2p {
    /// ModelStep - 模型推理步骤
    ///
    /// 使用 AI 模型生成发音
    ///
    /// 连字符词（如 hello-world）未命中整词词典时，会拆分为多个片段：
    /// 片段先查随附字典（配置了 file 时），字典查不到的片段才走模型推理，
    /// 最后合并为单个音素序列；任一片段失败则整个词失败。
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

        void cleanup() override;

    private:
        // T7: centralized batch size default.
        static constexpr int kModelStepBatchSize = 50;

        bool                          m_enabled = true;
        std::string                   m_onnxG2pId;
        int                           m_batchSize = kModelStepBatchSize;
        std::string                   m_langRef; // optional lang_ref override passed to the model
        srt::core::NO<srt::g2p::Task> m_onnxTask;

        // 连字符词片段的随附字典（可选）。配置参数 file，与 DictStep 同一致；
        // 加载失败或未配置时退化为片段全部走模型推理。
        srt::g2p::PhonemeDict m_phonemeDict;
        bool                  m_dictLoaded = false;

        /// 使用模型推理一批单词（可能为拆分后的片段），
        /// 结果按 flat 顺序写入 prons/cands/errors（与 words 对齐）。
        void processBatch(const std::vector<std::string> &words, std::vector<std::string> &prons,
                          std::vector<std::vector<std::string>> &cands, std::vector<srt::g2p::G2pErrorType> &errors);
    };

} // namespace srt::g2p::plugins::ChainG2p
