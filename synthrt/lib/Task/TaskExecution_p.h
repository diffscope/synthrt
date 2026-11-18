#ifndef SYNTHRT_TASK_TASKEXECUTION_P_H
#define SYNTHRT_TASK_TASKEXECUTION_P_H

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "ITask.h"
#include "TaskHandle.h"

namespace srt::detail {

    class TaskExecution final : public std::enable_shared_from_this<TaskExecution> {
    public:
        using Body = std::function<Expected<std::unique_ptr<TaskResult>>(const TaskStopToken &)>;
        using Classifier =
            std::function<ITask::State(const Expected<std::unique_ptr<TaskResult>> &, bool)>;
        using StopHook = std::function<Expected<void>()>;
        using WaitHook = std::function<Expected<void>()>;
        using WorkerHook = std::function<Expected<void>(std::function<void()>)>;

        TaskExecution(Classifier classifier, StopHook stopHook, WaitHook waitHook,
                      WorkerHook workerHook)
            : m_classifier(std::move(classifier)), m_stopHook(std::move(stopHook)),
              m_waitHook(std::move(waitHook)), m_workerHook(std::move(workerHook)) {
        }

        Expected<std::unique_ptr<TaskResult>> start(Body body);

        Expected<void> startAsync(Body body, ITask::AsyncCallback callback);

        Expected<void> stop();

        Expected<void> wait();

        void waitForExecution() noexcept;

        void detach();

        bool isActive() const;

        bool isFinished() const;

        bool isDeliveringCallbackOnThisThread() const;

        ITask::State state() const noexcept;

        void setState(ITask::State state) noexcept;

    private:
        void release();

        Classifier m_classifier;
        StopHook m_stopHook;
        WaitHook m_waitHook;
        WorkerHook m_workerHook;
        TaskStopSource m_stopSource;
        std::atomic<ITask::State> m_state = ITask::Idle;

        mutable std::mutex m_mutex;
        mutable std::recursive_mutex m_hookMutex;
        std::condition_variable m_finished;
        bool m_detached = false;
        bool m_running = false;
        std::thread::id m_workerId;
        std::atomic<std::thread::id> m_callbackWorker;
    };

}

#endif // SYNTHRT_TASK_TASKEXECUTION_P_H
