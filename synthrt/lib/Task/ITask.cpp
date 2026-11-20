#include "ITask.h"

#include <atomic>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include "TaskExecution_p.h"

namespace srt {

    class ITask::Impl {
    public:
        TaskHandle makeExecution(ITask &task) {
            auto execution = std::make_shared<detail::TaskExecution>(
                [&task](const Expected<std::unique_ptr<TaskResult>> &outcome, bool stopRequested) {
                    return task.classifyOutcome(outcome, stopRequested);
                },
                [&task] { return task.quit(); }, [&task] { return task.wait(); },
                [&task](std::function<void()> work) { return task.startWorker(std::move(work)); });
            return TaskHandle(std::move(execution));
        }

        mutable std::mutex mutex;
        TaskHandle current;
        std::atomic<State> state = State::Idle;
        ContribContract inputContract;
    };

    ITask::ITask() : _impl(std::make_unique<Impl>()) {
    }

    ITask::ITask(ContribContract inputContract) : ITask() {
        _impl->inputContract = std::move(inputContract);
    }

    ITask::~ITask() {
        TaskHandle current;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            current = _impl->current;
        }
        if (current.m_execution) {
            current.m_execution->waitForExecution();
            current.m_execution->detach();
        }
    }

    Expected<void> ITask::initialize(const TaskInitArgs &) {
        return {};
    }

    Expected<std::unique_ptr<TaskResult>> ITask::start(const TaskStartInput &input) {
        if (auto checked = validateInput(input); !checked) {
            return checked.takeError();
        }

        TaskHandle execution;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            if (_impl->current.m_execution && _impl->current.isActive() &&
                !_impl->current.m_execution->isDeliveringCallbackOnThisThread()) {
                return Error(Error::InvalidArgument, "this Task already has an active execution");
            }
            execution = _impl->makeExecution(*this);
            _impl->current = execution;
        }

        auto body = [this, &input](const TaskStopToken &token) { return runBody(input, token); };
        return execution.m_execution->start(std::move(body));
    }

    Expected<TaskHandle> ITask::startAsync(std::shared_ptr<const TaskStartInput> input,
                                           AsyncCallback callback) {
        if (!input) {
            return Error(Error::InvalidArgument, "asynchronous Task input must not be null");
        }
        if (auto checked = validateInput(*input); !checked) {
            return checked.takeError();
        }

        TaskHandle execution;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            if (_impl->current.m_execution && _impl->current.isActive() &&
                !_impl->current.m_execution->isDeliveringCallbackOnThisThread()) {
                return Error(Error::InvalidArgument, "this Task already has an active execution");
            }
            execution = _impl->makeExecution(*this);
            _impl->current = execution;
        }

        auto body = [this, input = std::move(input)](const TaskStopToken &token) {
            return runBody(*input, token);
        };
        if (auto started = execution.m_execution->startAsync(std::move(body), std::move(callback));
            !started) {
            return started.takeError();
        }
        return execution;
    }

    Expected<void> ITask::stop() {
        TaskHandle current;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            current = _impl->current;
        }
        return current.valid() ? current.stop() : Expected<void>();
    }

    Expected<void> ITask::waitForFinished() {
        TaskHandle current;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            current = _impl->current;
        }
        return current.valid() ? current.wait() : Expected<void>();
    }

    ITask::State ITask::state() const noexcept {
        TaskHandle current;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            current = _impl->current;
        }
        return current.m_execution ? current.m_execution->state()
                                   : _impl->state.load(std::memory_order_acquire);
    }

    Expected<void> ITask::validateInput(const TaskStartInput &input) const {
        if (!_impl->inputContract.interfaceId().empty() &&
            input.contract() != _impl->inputContract) {
            return Error(Error::InvalidArgument,
                         "this Task accepts " + _impl->inputContract.toString() +
                             " input, and the input is " + input.contract().toString());
        }
        return {};
    }

    Expected<void> ITask::quit() {
        return {};
    }

    Expected<void> ITask::wait() {
        return {};
    }

    ITask::State ITask::classifyOutcome(const Expected<std::unique_ptr<TaskResult>> &outcome,
                                        bool) noexcept {
        if (outcome) {
            return Succeeded;
        }
        return outcome.error().code() == Error::Canceled ? Canceled : Failed;
    }

    Expected<void> ITask::startWorker(std::function<void()> work) {
        try {
            std::thread(std::move(work)).detach();
        } catch (const std::system_error &error) {
            return Error(error.code(), "failed to start Task worker: " + std::string(error.what()));
        }
        return {};
    }

    void ITask::setState(State state) noexcept {
        TaskHandle current;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            current = _impl->current;
        }
        if (current.m_execution) {
            current.m_execution->setState(state);
        } else {
            _impl->state.store(state, std::memory_order_release);
        }
    }

    Expected<std::unique_ptr<TaskResult>> ITask::runBody(const TaskStartInput &input,
                                                         const TaskStopToken &token) {
        try {
            return execute(input, token);
        } catch (const std::exception &error) {
            return Error(Error::Internal,
                         "Task execution threw an exception: " + std::string(error.what()));
        } catch (...) {
            return Error(Error::Internal, "Task execution threw an exception");
        }
    }

}
