#ifndef SRT_G2P_PLUGINS_LSTMG2P_INTERNAL_TASKIMPLBASE_H
#define SRT_G2P_PLUGINS_LSTMG2P_INTERNAL_TASKIMPLBASE_H

#include <synthrt/G2P/Task/VersionedTaskImplBase.h>
#include <memory>
#include <shared_mutex>
#include <map>
#include <string>
#include <filesystem>

#include <synthrt/G2P/Task/SessionTask.h>
#include <synthrt/G2P/Task/SessionFactory.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/G2P/Core/PackageManager.h>

namespace srt::g2p::plugins::LstmG2p::Internal
{
    /// V1/V2 共用的推理辅助函数：从 SessionResult 的 outputs 中按名取张量。
    srt::core::Expected<srt::core::NO<srt::core::ITensor>>
    getTensorFromResult(const srt::core::NO<srt::g2p::SessionResult> &result, const std::string &name);

    /// LstmG2p 的基类实现
    /// 包含 V1 和 V2 的共同代码
    class LstmG2pTaskImplBase : public srt::g2p::VersionedTaskImplBase {
    public:
        explicit LstmG2pTaskImplBase(const srt::g2p::ModuleSpec *spec);
        ~LstmG2pTaskImplBase() override = default;

        srt::core::Expected<void> initialize() override;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override = 0;

        std::string getConfig() const override;

    protected:
        const srt::g2p::ModuleSpec *m_spec;
        srt::core::NO<srt::g2p::SessionFactory> m_driver;
        srt::core::NO<srt::g2p::SessionTask> m_encoderSession;
        srt::core::NO<srt::g2p::SessionTask> m_decodeSession;
        mutable std::shared_mutex m_mutex;

        std::map<std::string, int> m_charVocab, m_phonemeVocab;
        std::map<int, std::string> m_idxToPhoneme;
        int m_unkIdx = 0;
        int m_padIdx = 1;
        int m_bosIdx = 2;
        int m_eosIdx = 3;
        int m_maxLen = 48;
        std::string m_config;
        bool m_driverAvailable = false;

        // 配置私有成员变量
        std::filesystem::path m_encoderPath;
        std::filesystem::path m_decoderPath;
        std::filesystem::path m_charVocabPath;
        std::filesystem::path m_phonemeVocabPath;

        // Helper function to load phoneme mapping from JSON file
        static srt::core::Expected<std::map<std::string, int>>
        loadPhonemeMapping(const std::filesystem::path &path, const std::string &fieldName);

        /// 当驱动不可用时，生成降级结果（原样复制 lyric）
        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        makeFallbackResult(const std::vector<std::string> &lyrics) const;
    };

} // namespace srt::g2p::plugins::LstmG2p::Internal

#endif // SRT_G2P_PLUGINS_LSTMG2P_INTERNAL_TASKIMPLBASE_H
