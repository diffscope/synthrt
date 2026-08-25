#include "InferenceExecutive.h"

#include "InferenceContrib.h"

namespace srt {

    InferenceExecutive::InferenceExecutive(InferenceSpec &spec) : ContribExecutive(spec) {
    }

    InferenceExecutive::~InferenceExecutive() = default;

    InferenceSpec &InferenceExecutive::spec() const {
        return *ContribExecutive::spec().as<InferenceSpec>();
    }

    Expected<void> InferenceExecutive::quit() {
        return stop();
    }

    Expected<void> InferenceExecutive::wait() {
        return waitForFinished();
    }

}
