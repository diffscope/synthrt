#include "ITask.h"

#include <exception>
#include <system_error>
#include <thread>
#include <utility>

#include "Logging.h"

namespace srt {

    ITask::ITask()
        : m_asyncState(std::make_shared<AsyncState>()),
          m_generation(std::make_shared<std::atomic<std::uint64_t>>(0)),
          m_callbackWorker(std::make_shared<std::atomic<std::thread::id>>()) {
    }

    ITask::~ITask() {
        waitForExecution();
    }

    Expected<void> ITask::initialize(const TaskInitArgs &) {
        return {};
    }

    Expected<std::unique_ptr<TaskResult>> ITask::start(const TaskStartInput &input) {
        if (auto checked = validateInput(input); !checked) {
            return checked.takeError();
        }

        bool borrowedClaim = false;
        {
            std::lock_guard<std::mutex> lock(m_asyncState->mutex);
            if (m_asyncState->running) {
                if (m_asyncState->workerId != std::this_thread::get_id() || !deliveringCallback()) {
                    return Error(Error::InvalidArgument,
                                 "this Task already has an active execution");
                }
                borrowedClaim = true;
                m_stopRequested.store(false, std::memory_order_release);
            } else {
                m_asyncState->running = true;
                m_asyncState->workerId = std::this_thread::get_id();
                ++*m_generation;
                m_stopRequested.store(false, std::memory_order_release);
            }
        }

        auto result = runClaimed(input);
        if (!borrowedClaim) {
            release();
        }
        return result;
    }

    Expected<void> ITask::startAsync(std::shared_ptr<const TaskStartInput> input,
                                     AsyncCallback callback) {
        if (!input) {
            return Error(Error::InvalidArgument, "asynchronous Task input must not be null");
        }
        if (!callback) {
            return Error(Error::InvalidArgument, "asynchronous Task callback must not be empty");
        }

        if (auto checked = validateInput(*input); !checked) {
            return checked.takeError();
        }

        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(m_asyncState->mutex);
            if (m_asyncState->running) {
                if (m_asyncState->workerId != std::this_thread::get_id() || !deliveringCallback()) {
                    return Error(Error::InvalidArgument,
                                 "this Task already has an active execution");
                }
            }
            m_asyncState->running = true;
            m_asyncState->workerId = {};
            generation = ++*m_generation;
        }
        m_stopRequested.store(false, std::memory_order_release);
        setState(Running);

        auto asyncState = m_asyncState;
        auto current = m_generation;
        auto callbackWorker = m_callbackWorker;
        try {
            startWorker([this, asyncState, current, callbackWorker, generation,
                         input = std::move(input), callback = std::move(callback)]() mutable {
                {
                    std::lock_guard<std::mutex> lock(asyncState->mutex);
                    asyncState->workerId = std::this_thread::get_id();
                }

                auto result = runClaimed(*input);

                callbackWorker->store(std::this_thread::get_id());
                try {
                    callback(std::move(result));
                } catch (const std::exception &error) {
                    logCategory().srtCritical(
                        "an asynchronous Task callback threw an exception: %1", error.what());
                } catch (...) {
                    logCategory().srtCritical("an asynchronous Task callback threw an exception");
                }

                auto self = std::this_thread::get_id();
                callbackWorker->compare_exchange_strong(self, std::thread::id());

                {
                    std::lock_guard<std::mutex> lock(asyncState->mutex);
                    if (current->load(std::memory_order_acquire) == generation) {
                        asyncState->running = false;
                        asyncState->workerId = {};
                    }
                }
                asyncState->finished.notify_all();
            });
        } catch (const std::system_error &error) {
            {
                std::lock_guard<std::mutex> lock(m_asyncState->mutex);
                if (m_generation->load(std::memory_order_acquire) == generation) {
                    m_asyncState->running = false;
                    m_asyncState->workerId = {};
                }
            }
            m_asyncState->finished.notify_all();
            setState(Failed);
            return workerStartError(error);
        }
        return {};
    }

    Expected<void> ITask::stop() {
        {
            std::lock_guard<std::mutex> lock(m_asyncState->mutex);
            if (!m_asyncState->running) {
                return {};
            }
            m_stopRequested.store(true, std::memory_order_release);
        }
        return quit();
    }

    Expected<void> ITask::waitForFinished() {
        waitForExecution();
        return wait();
    }

    Expected<void> ITask::validateInput(const TaskStartInput &) const {
        return {};
    }

    Expected<void> ITask::quit() {
        return {};
    }

    Expected<void> ITask::wait() {
        return {};
    }

    ITask::State
        ITask::classifyOutcome(const Expected<std::unique_ptr<TaskResult>> &outcome) noexcept {
        if (outcome) {
            return Succeeded;
        }
        return outcome.error().code() == Error::Canceled ? Canceled : Failed;
    }

    Error ITask::exceptionError(const std::exception &error) const {
        return Error(Error::Internal,
                     "Task execution threw an exception: " + std::string(error.what()));
    }

    Error ITask::unknownExceptionError() const {
        return Error(Error::Internal, "Task execution threw an exception");
    }

    Error ITask::workerStartError(const std::system_error &error) const {
        return Error(error.code(), "failed to start Task worker: " + std::string(error.what()));
    }

    void ITask::startWorker(std::function<void()> work) {
        std::thread(std::move(work)).detach();
    }

    void ITask::release() {
        {
            std::lock_guard<std::mutex> lock(m_asyncState->mutex);
            m_asyncState->running = false;
            m_asyncState->workerId = {};
        }
        m_asyncState->finished.notify_all();
    }

    Expected<std::unique_ptr<TaskResult>> ITask::runClaimed(const TaskStartInput &input) {
        setState(Running);
        Expected<std::unique_ptr<TaskResult>> result =
            Error(Error::Internal, "Task execution did not produce a result");
        try {
            result = execute(input);
        } catch (const std::exception &error) {
            result = exceptionError(error);
        } catch (...) {
            result = unknownExceptionError();
        }
        setState(classifyOutcome(result));
        return result;
    }

    bool ITask::claimedByThisThread() const {
        std::lock_guard<std::mutex> lock(m_asyncState->mutex);
        return m_asyncState->running && m_asyncState->workerId == std::this_thread::get_id();
    }

    bool ITask::deliveringCallback() const {
        return m_callbackWorker->load(std::memory_order_acquire) == std::this_thread::get_id();
    }

    void ITask::waitForExecution() noexcept {
        auto asyncState = m_asyncState;
        std::unique_lock<std::mutex> lock(asyncState->mutex);
        if (asyncState->running && asyncState->workerId == std::this_thread::get_id()) {
            return;
        }
        asyncState->finished.wait(lock, [&asyncState] { return !asyncState->running; });
    }

}
