#include "InferenceExecInstance.h"

#include "InferenceContrib.h"

namespace srt {

    InferenceExecInstance::InferenceExecInstance(InferenceSpec &spec) : ContribExecInstance(spec) {
    }

    InferenceExecInstance::~InferenceExecInstance() = default;

    InferenceSpec &InferenceExecInstance::spec() const {
        return *ContribExecInstance::spec().as<InferenceSpec>();
    }

    ITask::State InferenceExecInstance::state() const noexcept {
        return task().state();
    }

    Expected<void> InferenceExecInstance::stop() {
        return task().stop();
    }

    Expected<void> InferenceExecInstance::waitForFinished() {
        return task().waitForFinished();
    }

    Expected<void> InferenceExecInstance::quit() {
        return stop();
    }

    Expected<void> InferenceExecInstance::wait() {
        return waitForFinished();
    }

}
