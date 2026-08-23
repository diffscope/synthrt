#include "ITask.h"
#include "ITask_p.h"

#include <stdcorelib/pimpl.h>

namespace srt {

    ITask::ITask() : ITask(*new Impl()) {
    }

    ITask::~ITask() = default;

    Expected<void> ITask::initialize(const NO<TaskInitArgs> &args) {
        return Expected<void>();
    }

    Expected<void> ITask::startAsync(
        const NO<TaskStartInput> &input,
        const std::function<void(const NO<TaskResult> &, const Error &)> &callback) {
        return Expected<void>();
    }

    ITask::State ITask::state() const {
        stdc_impl_t;
        return impl.state;
    }

    void ITask::setState(State state) {
        stdc_impl_t;
        impl.state = state;
    }

    ITask::ITask(Impl &impl) : NamedObject(impl) {
    }


}
