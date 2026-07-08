#ifndef SRT_CORE_TASK_ITASK_H
#define SRT_CORE_TASK_ITASK_H

#include <functional>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>

namespace srt::core {

    /// TaskInfoBase - base class for all task-related info/input/result types.
    /// Extends NamedObject to provide object naming; serves as the common base
    /// for TaskConfiguration, TaskInitArgs, TaskStartInput and TaskResult.
    class SRT_CORE_EXPORT TaskInfoBase : public NamedObject {
    public:
        TaskInfoBase() = default;
        explicit inline TaskInfoBase(std::string name) : NamedObject(std::move(name)) {
        }
        ~TaskInfoBase() override = default;
    };

    /// TaskConfiguration - configuration object carried by ModuleSpec.
    /// Extends TaskInfoBase (which extends NamedObject) so it can be wrapped
    /// in NO<TaskConfiguration>. See ModuleSpec::configuration().
    class SRT_CORE_EXPORT TaskConfiguration : public TaskInfoBase {
    public:
        TaskConfiguration() = default;
        explicit inline TaskConfiguration(std::string name) : TaskInfoBase(std::move(name)) {
        }
    };

    class TaskInitArgs : public TaskInfoBase {
    public:
        inline TaskInitArgs(std::string name) : TaskInfoBase(std::move(name)) {
        }
    };

    class TaskStartInput : public TaskInfoBase {
    public:
        inline TaskStartInput(std::string name) : TaskInfoBase(std::move(name)) {
        }
    };

    class TaskResult : public TaskInfoBase {
    public:
        inline TaskResult(std::string name) : TaskInfoBase(std::move(name)) {
        }

        Error error;
    };

    class SRT_CORE_EXPORT ITask : public NamedObject {
    public:
        ITask();
        ~ITask();

        enum State {
            Idle,
            Running,
            Failed,
            Terminated,
        };

        using StartAsyncCallback = std::function<void(const NO<TaskResult> &, const Error &)>;

    public:
        virtual Expected<void> initialize(const NO<TaskInitArgs> &args);

        virtual Expected<NO<TaskResult>> start(const NO<TaskStartInput> &input) = 0;
        virtual Expected<void> startAsync(const NO<TaskStartInput> &input,
                                          const StartAsyncCallback &callback);
        virtual bool stop() = 0;

        State state() const;

        virtual NO<TaskResult> result() const = 0;

    protected:
        void setState(State state);

    protected:
        class Impl;
        ITask(Impl &impl);
    };

}

#endif // SRT_CORE_TASK_ITASK_H
