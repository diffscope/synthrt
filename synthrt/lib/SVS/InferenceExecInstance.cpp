#include "InferenceExecInstance.h"

#include "InferenceContrib.h"

namespace srt {

    InferenceExecInstance::InferenceExecInstance(InferenceSpec &spec) : ContribExecInstance(spec) {
    }

    InferenceExecInstance::~InferenceExecInstance() = default;

    InferenceSpec &InferenceExecInstance::spec() const {
        return *ContribExecInstance::spec().as<InferenceSpec>();
    }

    Expected<void> InferenceExecInstance::quit() {
        return stop();
    }

    Expected<void> InferenceExecInstance::wait() {
        return waitForFinished();
    }

}
