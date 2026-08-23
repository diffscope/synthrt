#include "Inference.h"

#include <stdcorelib/pimpl.h>

#include "InferenceContrib.h"
#include "ITask_p.h"
#include "PackageHandle.h"

namespace srt {

    class Inference::Impl : public ITask::Impl {
    public:
        explicit Impl(const InferenceSpec *spec) : spec(spec) {
        }

        const InferenceSpec *spec;
    };

    Inference::Inference(const InferenceSpec *spec) : ITask(*new Impl(spec)) {
    }

    Inference::~Inference() = default;

    const InferenceSpec *Inference::spec() const {
        stdc_impl_t;
        return impl.spec;
    }

    SynthUnit &Inference::synthUnit() const {
        stdc_impl_t;
        return impl.spec->package().synthUnit();
    }

}
