#ifndef SYNTHRT_INFERENCEEXECINSTANCE_H
#define SYNTHRT_INFERENCEEXECINSTANCE_H

#include <synthrt/Core/ContribExecInstance.h>
#include <synthrt/Core/ContribSpecPayload.h>
#include <synthrt/Task/ITask.h>

namespace srt {

    class InferenceSpec;

    /// Runtime options supplied when an inference instance is created.
    class InferenceRuntimeOptions : public ContribSpecPayload {
    public:
        ~InferenceRuntimeOptions() = default;

    protected:
        using ContribSpecPayload::ContribSpecPayload;
    };

    class InferenceInitArgs : public TaskInitArgs {
    public:
        InferenceInitArgs(std::string type, int version) : TaskInitArgs(std::move(type), version) {
        }
    };

    class SYNTHRT_EXPORT InferenceExecInstance : public ITask, public ContribExecInstance {
    public:
        explicit InferenceExecInstance(InferenceSpec &spec);
        ~InferenceExecInstance();

    public:
        InferenceSpec &spec() const;

    protected:
        Expected<void> quit() override;
        Expected<void> wait() override;
    };

}

#endif // SYNTHRT_INFERENCEEXECINSTANCE_H
