#include "ITask.h"

#include <exception>
#include <system_error>
#include <thread>
#include <utility>

#include "Logging.h"

namespace srt {

    ITask::ITask() : m_asyncState(std::make_shared<AsyncState>()) {
    }

    ITask::~ITask() {
        waitForAsyncExecution();
    }

    Expected<void> ITask::initialize(const TaskInitArgs &) {
        return {};
    }

    Expected<void> ITask::startAsync(std::shared_ptr<const TaskStartInput> input,
                                     AsyncCallback callback) {
        if (!input) {
            return Error(Error::InvalidArgument, "asynchronous Task input must not be null");
        }
        if (!callback) {
            return Error(Error::InvalidArgument, "asynchronous Task callback must not be empty");
        }

        auto asyncState = m_asyncState;
        {
            std::lock_guard<std::mutex> lock(asyncState->mutex);
            if (asyncState->running) {
                return Error(Error::InvalidArgument,
                             "an asynchronous Task execution is already active");
            }
            asyncState->running = true;
            asyncState->cancellationRequested = false;
            asyncState->workerId = {};
        }
        setState(Running);

        try {
            std::thread([this, asyncState, input = std::move(input),
                         callback = std::move(callback)]() mutable {
                const auto finish = [&asyncState] {
                    {
                        std::lock_guard<std::mutex> lock(asyncState->mutex);
                        asyncState->running = false;
                        asyncState->workerId = {};
                    }
                    asyncState->finished.notify_all();
                };
                {
                    std::lock_guard<std::mutex> lock(asyncState->mutex);
                    asyncState->workerId = std::this_thread::get_id();
                }

                Expected<std::unique_ptr<TaskResult>> result =
                    Error(Error::InvalidFormat, "Task execution did not produce a result");
                try {
                    result = start(*input);
                } catch (const std::exception &error) {
                    setState(Failed);
                    result = Error(Error::InvalidFormat, "Task execution threw an exception: " +
                                                             std::string(error.what()));
                } catch (...) {
                    setState(Failed);
                    result = Error(Error::InvalidFormat, "Task execution threw an exception");
                }

                {
                    std::lock_guard<std::mutex> lock(asyncState->mutex);
                    if (asyncState->cancellationRequested) {
                        setState(Canceled);
                    }
                }

                try {
                    callback(std::move(result));
                } catch (const std::exception &error) {
                    finish();
                    logCategory().srtFatal("asynchronous Task callback threw an exception: %1",
                                           error.what());
                    return;
                } catch (...) {
                    finish();
                    logCategory().srtFatal("asynchronous Task callback threw an exception");
                    return;
                }
                finish();
            }).detach();
        } catch (const std::system_error &error) {
            {
                std::lock_guard<std::mutex> lock(asyncState->mutex);
                asyncState->running = false;
            }
            asyncState->finished.notify_all();
            setState(Failed);
            return Error(error.code(), "failed to start Task worker: " + std::string(error.what()));
        }
        return {};
    }

    void ITask::requestAsyncCancellation() noexcept {
        std::lock_guard<std::mutex> lock(m_asyncState->mutex);
        if (m_asyncState->running) {
            m_asyncState->cancellationRequested = true;
        }
    }

    void ITask::waitForAsyncExecution() noexcept {
        auto asyncState = m_asyncState;
        std::unique_lock<std::mutex> lock(asyncState->mutex);
        if (asyncState->running && asyncState->workerId == std::this_thread::get_id()) {
            return;
        }
        asyncState->finished.wait(lock, [&asyncState] { return !asyncState->running; });
    }

}
