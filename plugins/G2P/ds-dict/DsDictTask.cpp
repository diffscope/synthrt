#include "DsDictTask.h"
#include "internal/V1/TaskImpl.h"
#include <synthrt/Core/Support/Logging.h>

namespace srt::g2p::plugins::DsDict
{
    SRT_G2P_TASK_IMPLEMENT(DsDictTask, Internal::V1::DsDictTaskImpl)

} // namespace srt::g2p::plugins::DsDict
