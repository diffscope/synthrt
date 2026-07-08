#ifndef SRT_G2P_PLUGINS_LSTMG2P_INTERNAL_V2_TASKIMPL_H
#define SRT_G2P_PLUGINS_LSTMG2P_INTERNAL_V2_TASKIMPL_H

#include "../TaskImplBase.h"
#include <memory>
#include <shared_mutex>
#include <map>
#include <string>

#include <synthrt/G2P/Task/SessionTask.h>
#include <synthrt/G2P/Task/SessionFactory.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/G2P/Core/PackageManager.h>

namespace srt::g2p::plugins::LstmG2p::Internal::V2
{
    /// LstmG2p 的 Level 2 实现
    /// 使用 LSTM 模型进行文本到音素的转换（支持批量推理）
    class LstmG2pTaskImpl final : public Internal::LstmG2pTaskImplBase {
    public:
        using Internal::LstmG2pTaskImplBase::LstmG2pTaskImplBase;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override;
    };

} // namespace srt::g2p::plugins::LstmG2p::Internal::V2

#endif // SRT_G2P_PLUGINS_LSTMG2P_INTERNAL_V2_TASKIMPL_H
