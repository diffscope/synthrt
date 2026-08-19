#pragma once

#include "../Core/G2pStep.h"
#include <string>
#include <vector>

namespace srt::g2p::plugins::ChainG2p {
    /// FormatStep - 格式化步骤
    ///
    /// 1) 歌词清洗（cleaner）：对仍需转换的词应用 cleaner 操作（当前支持
    ///    "lowercase"，对应 Cleaner-Eng 配置的 operations），结果写入
    ///    cleanedLyric，原词 lyric 保持不变。这是链中唯一允许对歌词做
    ///    清洗/修改的步骤，其他步骤不得修改歌词。
    /// 2) 发音结果格式化（stripTrailingSpace / addSpaceBetweenPhones 等）。
    class FormatStep : public G2pStep {
    public:
        FormatStep() = default;
        ~FormatStep() override = default;

        srt::core::Expected<void> configure(const srt::g2p::ModuleSpec *spec,
                                            const srt::core::JsonObject &config) override;

        void handle(G2pContext &context) override;

        std::string name() const override { return "format"; }

    private:
        bool m_stripTrailingSpace = false;
        bool m_normalizeTones = false;
        bool m_addSpaceBetweenPhones = false;
        std::vector<std::string> m_cleanerOperations;

        static std::string stripTrailingSpace(const std::string &str);
        static std::string addSpaceAtAlnumBoundary(const std::string &str);
        static std::string applyCleaner(const std::string &lyric,
                                        const std::vector<std::string> &operations);
    };

}
