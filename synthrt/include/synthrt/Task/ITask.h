#ifndef SYNTHRT_ITASK_H
#define SYNTHRT_ITASK_H

#include <functional>

#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/Error.h>

namespace srt {

    class TaskInitArgs : public NamedObject {
    public:
        inline TaskInitArgs(const std::string &name) : NamedObject(name) {
        }
    };

    class TaskStartInput : public NamedObject {
    public:
        inline TaskStartInput(const std::string &name) : NamedObject(name) {
        }
    };

    class TaskResult : public NamedObject {
    public:
        inline TaskResult(const std::string &name) : NamedObject(name) {
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
        virtual bool initialize(const NO<TaskInitArgs> &args, Error *error);

        virtual bool start(const NO<TaskStartInput> &input, Error *error) = 0;
        virtual bool startAsync(const NO<TaskStartInput> &input, const StartAsyncCallback &callback,
                                Error *error);
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