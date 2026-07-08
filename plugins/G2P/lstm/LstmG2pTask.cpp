#include "LstmG2pTask.h"
#include "internal/V1/TaskImpl.h"
#include "internal/V2/TaskImpl.h"
#include <synthrt/Core/Support/Logging.h>

namespace srt::g2p::plugins::LstmG2p
{
    LstmG2pTask::LstmG2pTask(const srt::g2p::ModuleSpec *spec)
        : srt::g2p::Task(spec), _manager(spec) {
        switch (spec->apiLevel()) {
            case 2:
                _manager.setImpl(std::make_unique<Internal::V2::LstmG2pTaskImpl>(spec), 2, 2);
                break;
            case 1:
                _manager.setImpl(std::make_unique<Internal::V1::LstmG2pTaskImpl>(spec), 1, 1);
                break;
            default:
                // A-8: 未知 Level 不静默降级到 V1，initialize() 会返回 NotImplementedError
                _manager.markLevelUnsupported(spec->apiLevel());
                break;
        }
    }

    SRT_G2P_TASK_IMPLEMENT_METHODS(LstmG2pTask)
} // namespace srt::g2p::plugins::LstmG2p
