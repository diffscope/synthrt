#include "Inference.h"

#include <stdcorelib/pimpl.h>

#include "InferenceContrib.h"
#include "ITask_p.h"
#include "PackageHandle.h"

namespace srt {

    class Inference::Impl : public ITask::Impl {};

    Inference::Inference(InferenceSpec &spec) : ITask(*new Impl()), ContribExecInstance(spec) {
    }

    Inference::~Inference() = default;

    InferenceSpec &Inference::spec() const {
        return *ContribExecInstance::spec().as<InferenceSpec>();
    }

    SynthUnit &Inference::synthUnit() const {
        return spec().package().synthUnit();
    }

}
