#ifndef SYNTHRT_TASK_ITASK_H
#define SYNTHRT_TASK_ITASK_H

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <synthrt/Support/Expected.h>
#include <synthrt/Task/TaskHandle.h>
#include <synthrt/Task/TaskStopToken.h>

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

        /// Transfers the identity of \a other into this payload.
        ///
        /// A caller that builds a payload and then hands it over needs those move operations, and
        /// the deleted copy operations of \c TaskPayload suppress the implicit ones. A class
        /// derived from this one must not declare a destructor either, because that suppresses
        /// its own move operations in the same way.
        TaskPayload(TaskPayload &&other) noexcept = default;
        TaskPayload &operator=(TaskPayload &&other) noexcept = default;

    private:
        std::string m_type;
        int m_version;

        STDC_DISABLE_COPY(TaskPayload)
    };

    /// Initialization data supplied before a Task is executed.
    class TaskInitArgs : public TaskPayload {
    protected:
        using TaskPayload::TaskPayload;
    };

    /// Input supplied for one Task execution.
    class TaskStartInput : public TaskPayload {
    protected:
        using TaskPayload::TaskPayload;
    };

    /// Successful output produced by one Task execution.
    class TaskResult : public TaskPayload {
    protected:
        using TaskPayload::TaskPayload;
    };

    /// A reusable synchronous or asynchronous execution object.
    ///
    /// Every Task allows one claimed execution at a time. The claim covers the body and, for an
    /// asynchronous call, its callback. A callback may destroy this object or start its successor.
    /// A stop request targets only an active claim. A successful final result determines the final
    /// state unless a derived class classifies that result as canceled.
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
        ///
        /// A second execution while another claim is active is rejected with \c InvalidArgument.
        /// A body exception is reported as \c Internal and leaves the Task reusable.
        Expected<std::unique_ptr<TaskResult>> start(const TaskStartInput &input);

        /// Starts one asynchronous execution.
        ///
        /// Shared ownership keeps \a input alive until the implementation no longer needs it. The
        /// callback receives either sole ownership of the result or the execution \c Error. The
        /// default implementation executes start() on a worker thread.
        Expected<TaskHandle> startAsync(std::shared_ptr<const TaskStartInput> input,
                                        AsyncCallback callback);

        /// Requests cancellation of the current execution.
        Expected<void> stop();

        /// Waits for the current execution to finish without ending the lifetime of this Task.
        Expected<void> waitForFinished();

        /// Returns the state of the current or most recently completed execution.
        State state() const noexcept;

    protected:
        ITask();

        /// Creates a Task that accepts start inputs of \a inputType and \a inputVersion.
        ITask(std::string inputType, int inputVersion);

        /// Executes the body for a claim that this class has already taken.
        virtual Expected<std::unique_ptr<TaskResult>> execute(const TaskStartInput &input,
                                                              const TaskStopToken &token) = 0;

        /// Requests that activity owned by this Task stop.
        ///
        /// This hook is called by stop() and must not wait. The default succeeds.
        virtual Expected<void> quit();

        /// Waits for activity owned by this Task after the common body and callback barrier.
        ///
        /// The default succeeds.
        virtual Expected<void> wait();

        /// Returns the final state for \a outcome.
        ///
        /// The default reports Failed for an error and Succeeded for a result. A contract that
        /// reports cancellation through its own error domain or through a partial result overrides
        /// this function.
        virtual State classifyOutcome(const Expected<std::unique_ptr<TaskResult>> &outcome,
                                      bool stopRequested) noexcept;

        /// Starts a detached worker that runs \a work.
        ///
        /// The function is virtual so that tests can simulate the absence of a worker.
        virtual Expected<void> startWorker(std::function<void()> work);

        /// Updates the state of the current execution while its body runs.
        void setState(State state) noexcept;

    private:
        Expected<void> validateInput(const TaskStartInput &input) const;

        Expected<std::unique_ptr<TaskResult>> runBody(const TaskStartInput &input,
                                                      const TaskStopToken &token);

        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY_MOVE(ITask)
    };

}

#endif // SYNTHRT_TASK_ITASK_H
