#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/VersionedTaskImplBase.h>
#include <synthrt/G2P/Task/SessionTask.h>
#include <synthrt/G2P/Task/SessionFactory.h>

#include "../BundleLoader.h"
#include "../LangIdMap.h"

namespace srt::g2p::plugins::Multig2p::Internal {
    /// Multig2pTaskImplBase - V1/V2 共享基类。
    ///
    /// 负责 bundle/vocabulary/lang_id_map 加载、ONNX session 生命周期管理与
    /// 推理参数读取。子类只需实现 start()。
    class Multig2pTaskImplBase : public srt::g2p::VersionedTaskImplBase {
    public:
        explicit Multig2pTaskImplBase(const srt::g2p::ModuleSpec *spec);
        ~Multig2pTaskImplBase() override = default;

        srt::core::Expected<void> initialize() override final;
        std::string getConfig() const override final;

    protected:
        const srt::g2p::ModuleSpec *m_spec;
        mutable std::shared_mutex m_mutex;

        srt::core::NO<srt::g2p::SessionFactory> m_driver;
        bool m_driverAvailable = false;

        srt::core::NO<srt::g2p::SessionTask> m_encoderSession;
        srt::core::NO<srt::g2p::SessionTask> m_decoderStepInitSession;
        srt::core::NO<srt::g2p::SessionTask> m_decoderStepSession;

        VocabularyData m_vocab;
        LangIdMap m_langIdMap;
        std::string m_defaultLangRef = "eng/default";

        int m_maxLen = 48;
        int m_beamSize = 1;
        int m_topK = 1;
        float m_lengthPenalty = 0.0f;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
            makeFallbackResult(const std::vector<std::string> &words) const;
    };

    /// 从 SessionResult::outputs 中按名称取出张量。
    srt::core::Expected<srt::core::NO<srt::core::ITensor>>
        getTensorFromResult(const srt::core::NO<srt::g2p::SessionResult> &result,
                            const std::string &name);

}
