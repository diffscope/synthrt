#ifndef SRT_G2P_TASK_G2PTASK_H
#define SRT_G2P_TASK_G2PTASK_H

#include <string>
#include <unordered_map>
#include <vector>

#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Task/Task.h>

namespace srt::g2p
{
    class G2pInputV1 : public TaskInput {
    public:
        G2pInputV1() : TaskInput("G2pInputV1") {}

        std::vector<std::string> g2pInput;
    };

    class G2pResultV1 : public TaskResult {
    public:
        G2pResultV1() : TaskResult("G2pResultV1") {}

        std::vector<G2pRes> g2pResult;
        std::string errorMessage;
    };
} // namespace srt::g2p

#endif // SRT_G2P_TASK_G2PTASK_H
