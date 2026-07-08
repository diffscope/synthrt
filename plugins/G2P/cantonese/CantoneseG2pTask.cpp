#include "CantoneseG2pTask.h"
#include <synthrt/Core/Support/Logging.h>
#include "internal/V1/TaskImpl.h"

namespace srt::g2p::plugins::CantoneseG2p
{
    SRT_G2P_TASK_IMPLEMENT(CantoneseG2pTask, Internal::V1::CantoneseG2pTaskImpl)

} // namespace srt::g2p::plugins::CantoneseG2p
