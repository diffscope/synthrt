#include "ITask.h"
#include "ITask_p.h"

#include <stdcorelib/pimpl.h>

namespace srt {

    ITask::ITask() : ITask(*new Impl(this)) {
    }

    ITask::~ITask() = default;

    bool ITask::initialize(const NO<TaskInitArgs> &args, Error *error) {
        return false;
    }

    bool ITask::startAsync(
        const NO<TaskStartInput> &input,
        const std::function<void(const NO<TaskResult> &, const Error &)> &callback, Error *error) {
        return false;
    }

    ITask::State ITask::state() const {
        __stdc_impl_t;
        return impl.state;
    }

    void ITask::setState(State state) {
        __stdc_impl_t;
        impl.state = state;
    }

    ITask::ITask(Impl &impl) : NamedObject(impl) {
    }


}