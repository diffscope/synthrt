#ifndef SYNTHRT_ITASK_H
#define SYNTHRT_ITASK_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <synthrt/Support/Expected.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    /// Identifies one typed value exchanged with a Task.
    class TaskPayload {
    public:
        virtual ~TaskPayload() = default;

        /// Returns the payload type understood by the Task implementation.
        const std::string &type() const noexcept {
            return m_type;
        }

        /// Returns the version of the payload type.
        int version() const noexcept {
            return m_version;
        }

        SYNTHRT_DECLARE_AS_METHODS(TaskPayload)

    protected:
        TaskPayload(std::string type, int version) : m_type(std::move(type)), m_version(version) {
        }

    private:
        std::string m_type;
        int m_version;

        STDC_DISABLE_COPY(TaskPayload)
    };

    /// Initialization data supplied before a Task is executed.
    class TaskInitArgs : public TaskPayload {
    public:
        virtual ~TaskInitArgs() = default;

    protected:
        using TaskPayload::TaskPayload;
    };

    /// Input supplied for one Task execution.
    class TaskStartInput : public TaskPayload {
    public:
        virtual ~TaskStartInput() = default;

    protected:
        using TaskPayload::TaskPayload;
    };

    /// Successful output produced by one Task execution.
    class TaskResult : public TaskPayload {
    public:
        virtual ~TaskResult() = default;

    protected:
        using TaskPayload::TaskPayload;
    };

    /// A reusable synchronous or asynchronous execution object.
    class SYNTHRT_EXPORT ITask {
    public:
        enum State {
            Idle,
            Running,
            Succeeded,
            Failed,
            Canceled,
        };

        using AsyncCallback = std::function<void(Expected<std::unique_ptr<TaskResult>> result)>;

        virtual ~ITask();

        /// Initializes this Task with typed arguments.
        ///
        /// The default implementation accepts the arguments without performing any work.
        virtual Expected<void> initialize(const TaskInitArgs &args);

        /// Executes this Task and transfers ownership of the successful result to the caller.
        virtual Expected<std::unique_ptr<TaskResult>> start(const TaskStartInput &input) = 0;

        /// Starts one asynchronous execution.
        ///
        /// Shared ownership keeps \a input alive until the implementation no longer needs it. The
        /// callback receives either sole ownership of the result or the execution \c Error.
        virtual Expected<void> startAsync(std::shared_ptr<const TaskStartInput> input,
                                          AsyncCallback callback);

        /// Requests cancellation of the current execution.
        virtual Expected<void> stop() = 0;

        /// Waits for the current execution to finish without ending the lifetime of this Task.
        virtual Expected<void> waitForFinished() = 0;

        /// Returns the state of the current or most recently completed execution.
        inline State state() const noexcept {
            return m_state.load();
        }

    protected:
        ITask();

        /// Updates the state exposed by state().
        inline void setState(State state) noexcept {
            m_state = state;
        }

        std::atomic<State> m_state = State::Idle;

    private:
        STDC_DISABLE_COPY(ITask)
    };

}

#endif // SYNTHRT_ITASK_H
