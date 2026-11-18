#include "TaskExecution_p.h"

#include <exception>
#include <thread>
#include <utility>

#include "Logging.h"

namespace srt::detail {

    Expected<std::unique_ptr<TaskResult>> TaskExecution::start(Body body) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running) {
                return Error(Error::InvalidArgument, "this Task execution is already active");
            }
            m_running = true;
            m_workerId = std::this_thread::get_id();
        }
        m_state.store(ITask::Running, std::memory_order_release);
        auto result = body(m_stopSource.token());
        m_state.store(m_classifier(result, m_stopSource.token().stopRequested()),
                      std::memory_order_release);
        release();
        return result;
    }

    Expected<void> TaskExecution::startAsync(Body body, ITask::AsyncCallback callback) {
        if (!callback) {
            return Error(Error::InvalidArgument, "asynchronous Task callback must not be empty");
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running) {
                return Error(Error::InvalidArgument, "this Task execution is already active");
            }
            m_running = true;
            m_workerId = {};
        }
        m_state.store(ITask::Running, std::memory_order_release);

        auto self = shared_from_this();
        auto started =
            m_workerHook([self, body = std::move(body), callback = std::move(callback)]() mutable {
                {
                    std::lock_guard<std::mutex> lock(self->m_mutex);
                    self->m_workerId = std::this_thread::get_id();
                }

                auto result = body(self->m_stopSource.token());
                self->m_state.store(
                    self->m_classifier(result, self->m_stopSource.token().stopRequested()),
                    std::memory_order_release);

                self->m_callbackWorker.store(std::this_thread::get_id());
                try {
                    callback(std::move(result));
                } catch (const std::exception &error) {
                    logCategory().srtCritical(
                        "an asynchronous Task callback threw an exception: %1", error.what());
                } catch (...) {
                    logCategory().srtCritical("an asynchronous Task callback threw an exception");
                }
                auto current = std::this_thread::get_id();
                self->m_callbackWorker.compare_exchange_strong(current, std::thread::id());
                self->release();
            });
        if (!started) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_running = false;
                m_workerId = {};
            }
            m_finished.notify_all();
            m_state.store(ITask::Failed, std::memory_order_release);
            return started.takeError();
        }
        return {};
    }

    Expected<void> TaskExecution::stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running) {
                return {};
            }
            m_stopSource.requestStop();
        }
        std::lock_guard<std::recursive_mutex> lock(m_hookMutex);
        if (m_detached) {
            return {};
        }
        return m_stopHook();
    }

    Expected<void> TaskExecution::wait() {
        waitForExecution();
        std::lock_guard<std::recursive_mutex> lock(m_hookMutex);
        if (m_detached) {
            return {};
        }
        return m_waitHook();
    }

    void TaskExecution::waitForExecution() noexcept {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_running && m_workerId == std::this_thread::get_id()) {
            return;
        }
        m_finished.wait(lock, [this] { return !m_running; });
    }

    void TaskExecution::detach() {
        std::lock_guard<std::recursive_mutex> lock(m_hookMutex);
        m_detached = true;
        m_classifier = {};
        m_stopHook = {};
        m_waitHook = {};
        m_workerHook = {};
    }

    bool TaskExecution::isActive() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

    bool TaskExecution::isFinished() const {
        return !isActive();
    }

    bool TaskExecution::isDeliveringCallbackOnThisThread() const {
        return m_callbackWorker.load(std::memory_order_acquire) == std::this_thread::get_id();
    }

    ITask::State TaskExecution::state() const noexcept {
        return m_state.load(std::memory_order_acquire);
    }

    void TaskExecution::setState(ITask::State state) noexcept {
        m_state.store(state, std::memory_order_release);
    }

    void TaskExecution::release() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
            m_workerId = {};
        }
        m_finished.notify_all();
    }

}
