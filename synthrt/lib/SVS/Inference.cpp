#include "Inference.h"

#include "InferenceContrib.h"

namespace srt {

    Inference::Inference(InferenceSpec &spec) : ContribExecInstance(spec) {
    }

    Inference::~Inference() = default;

    InferenceSpec &Inference::spec() const {
        return *ContribExecInstance::spec().as<InferenceSpec>();
    }

    Expected<void> Inference::quit() {
        return stop();
    }

    Expected<void> Inference::wait() {
        return waitForFinished();
    }

}
