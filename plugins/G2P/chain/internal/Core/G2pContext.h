#ifndef SRT_G2P_PLUGINS_CHAING2P_G2PCONTEXT_H
#define SRT_G2P_PLUGINS_CHAING2P_G2PCONTEXT_H

#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Package/Package.h>
#include <synthrt/Core/Module/Module.h>
#include <map>
#include <string>
#include <vector>

namespace srt::g2p::plugins::ChainG2p
{
    /// G2pContext - G2p 处理上下文
    ///
    /// 负责在处理步骤之间传递数据和状态
    class G2pContext {
    public:
        /// WordInfo - 单词信息
        struct WordInfo {
            // 原始信息
            std::string lyric;                    // 原词
            std::string cleanedLyric;             // 清洗后的词

            // 标记信息（来自 Tag 插件）
            std::string tag;                      // 标记类型
            std::string language;                 // 语言类型
            bool discard = false;                 // 是否丢弃

            // 处理模式
            std::string mode;                     // 处理模式（copy/convert/skip）

            // 结果信息
            std::string pronunciation;            // 发音结果
            std::vector<std::string> candidates;  // 候选发音
            srt::g2p::G2pErrorType errorType = srt::g2p::NoError;     // 错误类型

            // 来源标记
            bool fromDict = false;                // 是否来自字典
            bool fromModel = false;               // 是否来自模型
            bool fromFallback = false;            // 是否来自回退

            // 构造函数
            explicit WordInfo(std::string lyric) : lyric(std::move(lyric)) {}
        };

        /// 构造函数
        /// @param input 输入字符串数组
        explicit G2pContext(const std::vector<std::string> &input, const srt::g2p::ModuleSpec *spec)
            : m_spec(spec)
        {
            m_words.reserve(input.size());
            for (const auto &lyric : input) {
                m_words.emplace_back(WordInfo(lyric));
            }
        }

        /// 访问方法
        std::vector<WordInfo>& words() { return m_words; }
        const std::vector<WordInfo>& words() const { return m_words; }

        const srt::g2p::ModuleSpec* spec() const { return m_spec; }

        // TODO: ModuleSpec::Mgr() is not yet migrated to srt::core::ModuleSpec.
        // When ModuleSpec::Mgr() becomes available, restore:
        //   srt::g2p::PackageManager* mgr() const { return m_spec->Mgr(); }
        // Currently returns nullptr. Not used by any Step in ChainG2p pipeline.
        srt::g2p::PackageManager* mgr() const { return nullptr; }

        /// 控制方法
        bool isStopProcessing() const { return m_stopProcessing; }
        void setStopProcessing(bool stop) { m_stopProcessing = stop; }

    private:
        std::vector<WordInfo> m_words;
        const srt::g2p::ModuleSpec* m_spec;
        bool m_stopProcessing = false;
    };

} // namespace srt::g2p::plugins::ChainG2p

#endif // SRT_G2P_PLUGINS_CHAING2P_G2PCONTEXT_H
