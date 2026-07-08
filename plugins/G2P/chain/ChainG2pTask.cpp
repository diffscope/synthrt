#include "ChainG2pTask.h"
#include "internal/Core/G2pPipeline.h"
#include "internal/Core/G2pContext.h"
#include "internal/V1/TaskImpl.h"
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/Core/Support/Logging.h>

namespace srt::g2p::plugins::ChainG2p
{
    ChainG2pTask::ChainG2pTask(const srt::g2p::ModuleSpec *spec)
        : srt::g2p::Task(spec), _manager(spec) {
        auto impl = std::make_unique<Internal::V1::ChainG2pTaskImpl>(spec);
        impl->setTask(this);
        _manager.setImpl(std::move(impl));
    }

    SRT_G2P_TASK_IMPLEMENT_METHODS(ChainG2pTask)

} // namespace srt::g2p::plugins::ChainG2p
