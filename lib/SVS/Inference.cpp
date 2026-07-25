#include <synthrt/SVS/Inference.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/Core/Core/Runtime.h>

#include "Inference_p.h"

namespace srt::svs {

    Inference::Inference(const InferenceSpec *spec)
        : _impl(std::make_unique<Impl>()) {
        _impl->m_spec = spec;
    }

    Inference::~Inference() = default;

    const InferenceSpec *Inference::spec() const {
        return _impl->m_spec;
    }

    core::Runtime *Inference::SU() const {
        return _impl->m_spec ? _impl->m_spec->runtime() : nullptr;
    }

}
