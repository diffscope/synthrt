#ifndef SYNTHRT_TASK_ITASK_H
#define SYNTHRT_TASK_ITASK_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <synthrt/Support/Expected.h>

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
        Expected<void> startAsync(std::shared_ptr<const TaskStartInput> input,
                                  AsyncCallback callback);

        /// Requests cancellation of the current execution.
        Expected<void> stop();

        /// Waits for the current execution to finish without ending the lifetime of this Task.
        Expected<void> waitForFinished();

        /// Returns the state of the current or most recently completed execution.
        inline State state() const noexcept {
            return m_state.load();
        }

    protected:
        ITask();

        /// Executes the body for a claim that this class has already taken.
        virtual Expected<std::unique_ptr<TaskResult>> execute(const TaskStartInput &input) = 0;

        /// Rejects an input before a claim is taken.
        ///
        /// The default accepts every input. A typed Task overrides this function to compare the
        /// payload identity before converting the input.
        virtual Expected<void> validateInput(const TaskStartInput &input) const;

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
        virtual State
            classifyOutcome(const Expected<std::unique_ptr<TaskResult>> &outcome) noexcept;

        /// Converts an exception thrown by execute() into the Task's error domain.
        virtual Error exceptionError(const std::exception &error) const;

        /// Converts an unknown exception thrown by execute() into the Task's error domain.
        virtual Error unknownExceptionError() const;

        /// Converts a failure to create the asynchronous worker into the Task's error domain.
        virtual Error workerStartError(const std::system_error &error) const;

        /// Updates the state exposed by state().
        inline void setState(State state) noexcept {
            m_state = state;
        }

        /// Returns whether stop() has requested cancellation of the active claim.
        bool stopRequested() const noexcept {
            return m_stopRequested.load(std::memory_order_acquire);
        }

        /// Starts a detached worker that runs \a work.
        ///
        /// The function is virtual so that tests can simulate the absence of a worker.
        virtual void startWorker(std::function<void()> work);

    private:
        void release();

        Expected<std::unique_ptr<TaskResult>> runClaimed(const TaskStartInput &input);

        bool claimedByThisThread() const;

        bool deliveringCallback() const;

        void waitForExecution() noexcept;

        std::atomic<State> m_state = State::Idle;

        std::atomic_bool m_stopRequested = false;

        struct AsyncState {
            std::mutex mutex;
            std::condition_variable finished;
            bool running = false;
            std::thread::id workerId;
        };

        std::shared_ptr<AsyncState> m_asyncState;

        std::shared_ptr<std::atomic<std::uint64_t>> m_generation;

        std::shared_ptr<std::atomic<std::thread::id>> m_callbackWorker;

        STDC_DISABLE_COPY_MOVE(ITask)
    };

}

#endif // SYNTHRT_TASK_ITASK_H
