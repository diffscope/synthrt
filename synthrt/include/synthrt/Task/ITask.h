#ifndef SYNTHRT_ITASK_H
#define SYNTHRT_ITASK_H

#include <functional>

#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    class TaskInitArgs : public NamedObject {
    public:
        inline TaskInitArgs(std::string name) : NamedObject(std::move(name)) {
        }
    };

    class TaskStartInput : public NamedObject {
    public:
        inline TaskStartInput(std::string name) : NamedObject(std::move(name)) {
        }
    };

    class TaskResult : public NamedObject {
    public:
        inline TaskResult(std::string name) : NamedObject(std::move(name)) {
        }

        Error error;
    };

    class SYNTHRT_EXPORT ITask : public NamedObject {
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

#endif // SYNTHRT_ITASK_H