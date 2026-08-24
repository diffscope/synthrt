#include "ITask.h"

namespace srt {

    ITask::ITask() = default;

    ITask::~ITask() = default;

    Expected<void> ITask::initialize(const TaskInitArgs &) {
        return {};
    }

    Expected<void> ITask::startAsync(std::shared_ptr<const TaskStartInput>, AsyncCallback) {
        return Error(Error::FeatureNotSupported,
                     "this Task does not support asynchronous execution");
    }

}
