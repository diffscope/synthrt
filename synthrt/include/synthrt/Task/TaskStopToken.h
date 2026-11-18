#ifndef SYNTHRT_TASK_TASKSTOPTOKEN_H
#define SYNTHRT_TASK_TASKSTOPTOKEN_H

#include <atomic>
#include <memory>

#include <synthrt/synthrt_global.h>

namespace srt {

    class TaskStopSource;

    /// Observes the cancellation state of one Task execution.
    ///
    /// The token does not block and does not own the execution. Copies refer to the same stop
    /// state.
    class SYNTHRT_EXPORT TaskStopToken {
    public:
        TaskStopToken() = default;

        /// Returns whether stop was requested for the execution.
        bool stopRequested() const noexcept {
            return m_state && m_state->load(std::memory_order_acquire);
        }

    private:
        explicit TaskStopToken(std::shared_ptr<std::atomic_bool> state)
            : m_state(std::move(state)) {
        }

        std::shared_ptr<std::atomic_bool> m_state;

        friend class TaskStopSource;
    };

    /// Requests cancellation of one Task execution.
    ///
    /// This is the lightweight subset of the C++20 stop-token model. It carries no callbacks and
    /// exists so that a body can poll one execution-local state without depending on Task
    /// internals.
    class SYNTHRT_EXPORT TaskStopSource {
    public:
        TaskStopSource() : m_state(std::make_shared<std::atomic_bool>(false)) {
        }

        /// Requests cancellation of the execution observed by token().
        void requestStop() noexcept {
            m_state->store(true, std::memory_order_release);
        }

        /// Returns a token that observes this source.
        TaskStopToken token() const noexcept {
            return TaskStopToken(m_state);
        }

    private:
        std::shared_ptr<std::atomic_bool> m_state;
    };

}

#endif // SYNTHRT_TASK_TASKSTOPTOKEN_H
