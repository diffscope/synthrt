#include "ITask.h"

#include <stdcorelib/pimpl.h>

#include "ITask_p.h"

namespace srt::core {

    ITask::ITask() : ITask(*new Impl(this)) {
    }

    ITask::~ITask() = default;

    Expected<void> ITask::initialize(const NO<TaskInitArgs> &args) {
        return Expected<void>();
    }

    Expected<void> ITask::startAsync(const NO<TaskStartInput>                                         &input,
                                     const std::function<void(const NO<TaskResult> &, const Error &)> &callback) {
        return Expected<void>();
    }

    ITask::State ITask::state() const {
        stdc_impl_t;
        return impl.m_state;
    }

    void ITask::setState(State state) {
        stdc_impl_t;
        impl.m_state = state;
    }

    ITask::ITask(Impl &impl) : NamedObject(impl) {
    }

} // namespace srt::core
