#ifndef SYNTHRT_INFERENCEEXECUTIVE_H
#define SYNTHRT_INFERENCEEXECUTIVE_H

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

    class InferenceInitArgs : public TaskInitArgs {
    public:
        InferenceInitArgs(std::string type, int version) : TaskInitArgs(std::move(type), version) {
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
        InferenceSpec &spec() const;

        /// Returns the state of the contract-specific Task.
        virtual ITask::State state() const noexcept = 0;

        /// Requests cancellation of the current inference execution.
        virtual Expected<void> stop() = 0;

        /// Waits for the current inference execution to finish.
        virtual Expected<void> waitForFinished() = 0;

    private:
        Expected<void> quit() override;
        Expected<void> wait() override;
    };

}

#endif // SYNTHRT_INFERENCEEXECUTIVE_H
