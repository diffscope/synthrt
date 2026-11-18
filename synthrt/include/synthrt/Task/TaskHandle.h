#ifndef SYNTHRT_TASK_TASKHANDLE_H
#define SYNTHRT_TASK_TASKHANDLE_H

#include <memory>

#include <synthrt/Support/Expected.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    class ITask;

    namespace detail {
        class TaskExecution;
    }

    /// Observes and controls one asynchronous Task execution.
    ///
    /// A handle owns execution state, not the Task object. It remains valid when the Task is
    /// destroyed from the execution callback.
    class SYNTHRT_EXPORT TaskHandle {
    public:
        TaskHandle() = default;
        ~TaskHandle();

        /// Returns whether this handle refers to an execution.
        bool valid() const noexcept {
            return static_cast<bool>(m_execution);
        }

        explicit operator bool() const noexcept {
            return valid();
        }

        /// Returns whether the body or its callback is still running.
        bool isActive() const noexcept;

        /// Returns whether the body and its callback have finished.
        bool isFinished() const noexcept;

        /// Requests cancellation of this execution.
        Expected<void> stop();

        /// Waits for this execution and its callback, then for extra Task-owned activity.
        Expected<void> wait();

    private:
        explicit TaskHandle(std::shared_ptr<detail::TaskExecution> execution)
            : m_execution(std::move(execution)) {
        }

        std::shared_ptr<detail::TaskExecution> m_execution;

        friend class ITask;
    };

}

#endif // SYNTHRT_TASK_TASKHANDLE_H
