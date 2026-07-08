#ifndef SRT_G2P_PLUGINS_CHAING2P_STEPS_TAGANDVALIDATESTEP_H
#define SRT_G2P_PLUGINS_CHAING2P_STEPS_TAGANDVALIDATESTEP_H

#include "../Core/G2pStep.h"
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <InferUtil/Verifier.h>
#include <vector>
#include <string>
#include <memory>
#include <re2/re2.h>

namespace srt::g2p::plugins::ChainG2p
{
    /// TagAndValidateStep - 标记和验证步骤
    ///
    /// 使用正则表达式对输入词进行分类，决定 copy/convert 模式
    class TagAndValidateStep : public G2pStep {
    public:
        TagAndValidateStep() = default;
        ~TagAndValidateStep() override = default;

        srt::core::Expected<void> configure(const srt::g2p::ModuleSpec *spec,
                                            const srt::core::JsonObject &config) override;

        void handle(G2pContext &context) override;

        std::string name() const override { return "tagAndValidate"; }

        void cleanup() override {}

    private:
        // P-14: 复用 InferUtil::VerifyEntry 类型（ARCH-04 概念去重）。
        // 配置 JSON 格式与 InferUtil 不同（ChainG2p 用 "action" 字段，InferUtil 用 "mode"），
        // 因此解析与验证逻辑仍保留在本类中，仅统一结构体定义。
        using VerifyEntry = srt::g2p::plugins::InferUtil::VerifyEntry;

        struct CompiledVerifyEntry {
            std::unique_ptr<RE2> regex;
            std::string mode;
        };

        std::vector<VerifyEntry> m_verifyEntries;
        std::vector<CompiledVerifyEntry> m_compiledEntries;

        bool verifyWord(const std::string &word, std::string &mode) const;
        void compileEntries();
    };

} // namespace srt::g2p::plugins::ChainG2p

#endif // SRT_G2P_PLUGINS_CHAING2P_STEPS_TAGANDVALIDATESTEP_H
