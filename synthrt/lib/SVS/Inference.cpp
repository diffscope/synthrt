#include "Inference.h"

#include <stdcorelib/pimpl.h>

#include "InferenceContrib.h"
#include "AbstractTask_p.h"

namespace srt {

    class Inference::Impl : public AbstractTask::Impl {
    public:
        Impl(Inference *decl, const InferenceSpec *spec) : AbstractTask::Impl(decl), spec(spec) {
        }

        const InferenceSpec *spec;
    };

    Inference::Inference(const InferenceSpec *spec) : AbstractTask(*new Impl(this, spec)) {
    }

    Inference::~Inference() = default;

    const InferenceSpec *Inference::spec() const {
        __stdc_impl_t;
        return impl.spec;
    }

    SynthUnit *Inference::SU() const {
        __stdc_impl_t;
        return impl.spec->SU();
    }

}