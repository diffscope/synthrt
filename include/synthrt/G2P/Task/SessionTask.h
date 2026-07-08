#ifndef SRT_G2P_TASK_SESSIONTASK_H
#define SRT_G2P_TASK_SESSIONTASK_H

#include <filesystem>
#include <map>
#include <set>

#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/G2P/Task/Task.h>

namespace srt::g2p
{
    enum ExecutionProvider {
        CPUExecutionProvider = 0,
        CUDAExecutionProvider,
        DMLExecutionProvider,
        CoreMLExecutionProvider,
    };

    class DriverInitArgs : public TaskInitArgs {
    public:
        DriverInitArgs() : TaskInitArgs("DriverInitArgs") {}

        /// Load from progress
        bool loadFromProcess = false;

        /// The execution provider to use.
        ExecutionProvider ep = CPUExecutionProvider;

        /// The device index to use for CUDAExecutionProvider. (-1 means auto-select)
        int deviceIndex = -1;

        /// The onnxruntime library directory. (empty means use the default)
        std::filesystem::path runtimePath;
    };

    class SessionOpenArgs : public TaskInitArgs {
    public:
        SessionOpenArgs() : TaskInitArgs("SessionOpenArgs") {}

        /// Whether to force the use of the CPU for the session.
        bool useCpu = false;
    };

    class SessionStartInput : public TaskInput {
    public:
        SessionStartInput() : TaskInput("SessionStartInput") {}

        /// The input port names and the input tensors.
        std::map<std::string, srt::core::NO<srt::core::ITensor>> inputs;

        /// The output port names.
        std::set<std::string> outputs;
    };

    class SessionResult : public TaskResult {
    public:
        SessionResult() : TaskResult("SessionResult") {}

        std::map<std::string, srt::core::NO<srt::core::ITensor>> outputs;
    };
} // namespace srt::g2p

#endif // SRT_G2P_TASK_SESSIONTASK_H
