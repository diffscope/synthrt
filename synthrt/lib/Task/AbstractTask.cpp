#include "AbstractTask.h"
#include "AbstractTask_p.h"

#include <stdcorelib/pimpl.h>

namespace srt {

    AbstractTask::AbstractTask() : AbstractTask(*new Impl(this)) {
    }

    AbstractTask::~AbstractTask() = default;

    bool AbstractTask::initialize(const NO<TaskInitArgs> &args, Error *error) {
        return false;
    }

    bool AbstractTask::startAsync(
        const NO<TaskStartInput> &input,
        const std::function<void(const NO<TaskResult> &, const Error &)> &callback, Error *error) {
        return false;
    }

    AbstractTask::State AbstractTask::state() const {
        __stdc_impl_t;
        return impl.state;
    }

    void AbstractTask::setState(State state) {
        __stdc_impl_t;
        impl.state = state;
    }

    AbstractTask::AbstractTask(Impl &impl) : Object(impl) {
    }


}