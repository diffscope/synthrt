#ifndef SYNTHRT_SVS_INFERENCEEXECUTIVE_H
#define SYNTHRT_SVS_INFERENCEEXECUTIVE_H

#include <synthrt/Core/ContribExecutive.h>
#include <synthrt/Task/ITask.h>

namespace srt {

    class InferenceSpec;

    /// Runtime options supplied when an inference executive is created.
    class InferenceRuntimeOptions : public ContribRuntimeOptions {
    public:
        ~InferenceRuntimeOptions() = default;

    protected:
        using ContribRuntimeOptions::ContribRuntimeOptions;
    };

    /// Initialization data supplied to an inference executive.
    class InferenceInitArgs : public TaskInitArgs {
    public:
        explicit InferenceInitArgs(ContribContract contract) : TaskInitArgs(std::move(contract)) {
        }
    };

    /// A loaded runtime executive of one inference contribution.
    ///
    /// Each concrete executive exposes one contract-specific inference Task. The untyped Task is
    /// available only to derived classes so callers cannot bypass the contract-specific API.
    class SYNTHRT_EXPORT InferenceExecutive : public ContribExecutive {
    public:
        explicit InferenceExecutive(InferenceSpec &spec);
        ~InferenceExecutive();

    public:
        /// Returns the inference declaration this executive runs.
        InferenceSpec &spec() const;

        /// Returns the state of the contract-specific Task.
        virtual ITask::State state() const noexcept = 0;

    protected:
        /// Requests cancellation of activity owned by this executive.
        Expected<void> quit() override = 0;

        /// Waits for activity owned by this executive.
        Expected<void> wait() override = 0;
    };

}

#endif // SYNTHRT_SVS_INFERENCEEXECUTIVE_H
