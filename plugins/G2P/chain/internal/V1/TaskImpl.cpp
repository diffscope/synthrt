#include "TaskImpl.h"
#include <mutex>
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Task/G2pTask.h>
#include "../Core/G2pStep.h"

namespace srt::g2p::plugins::ChainG2p::Internal::V1 {
    ChainG2pTaskImpl::ChainG2pTaskImpl(const srt::g2p::ModuleSpec *spec)
        : m_spec(spec) {}

    srt::core::Expected<void> ChainG2pTaskImpl::initialize()
    {
        std::unique_lock lock(m_mutex);

        auto cfg = srt::core::config(m_spec);

        // 创建管道，传递 Task 对象
        m_pipeline = std::make_unique<G2pPipeline>(m_spec, m_task);

        // 配置管道
        auto configExp = m_pipeline->configure(cfg.raw());
        if (!configExp) {
            return configExp.takeError();
        }

        return {};
    }

    srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
    ChainG2pTaskImpl::start(const srt::core::NO<srt::g2p::TaskInput> &input)
    {
        if (!input) {
            return srt::g2p::Error(srt::g2p::Error::ConfigError, "g2p input is nullptr");
        }

        const auto g2pInput = input.as<srt::g2p::G2pInputV1>();
        if (!g2pInput) {
            return srt::g2p::Error(srt::g2p::Error::ValidationError,
                                   "type mismatch, expected G2pInputV1");
        }

        // 创建上下文
        auto context = std::make_shared<G2pContext>(g2pInput->g2pInput, m_spec);

        // 执行管道
        m_pipeline->process(*context);

        // 构建结果
        auto result = srt::core::NO<srt::g2p::G2pResultV1>::create();
        result->g2pResult.reserve(context->words().size());

        for (const auto &word : context->words()) {
            srt::g2p::G2pRes res;
            res.lyric = word.lyric;
            res.g2pId = m_spec->id();
            res.pronunciation = word.pronunciation;
            res.candidates = word.candidates;
            res.mode = word.mode;
            res.errorType = word.errorType;

            // copy 模式下，如果发音为空，应该原样返回
            if (res.mode == srt::g2p::kG2pModeCopy && res.pronunciation.empty()) {
                res.pronunciation = res.lyric;
                if (res.candidates.empty()) {
                    res.candidates = {res.lyric};
                }
            }

            result->g2pResult.push_back(res);
        }

        return result;
    }

    std::string ChainG2pTaskImpl::getConfig() const
    {
        // P-10: ChainG2p 无运行时配置 JSON（上游不消费 getConfig() 返回值）。
        // 配置由 pipeline 在 initialize() 中通过 m_pipeline->configure() 消费，
        // 不向外暴露。返回空串与 DsDict 保持一致（ARCH-04）。
        return {};
    }

}
