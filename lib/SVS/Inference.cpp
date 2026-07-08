#include <synthrt/SVS/Inference.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/Core/Core/Runtime.h>

namespace srt::svs {

    class Inference::Impl {
    public:
        const InferenceSpec *spec = nullptr;
    };

    Inference::Inference(const InferenceSpec *spec)
        : _impl(std::make_unique<Impl>()) {
        _impl->spec = spec;
    }

    Inference::~Inference() = default;

    const InferenceSpec *Inference::spec() const {
        return _impl->spec;
    }

    core::Runtime *Inference::SU() const {
        return _impl->spec ? _impl->spec->runtime() : nullptr;
    }

}
