#pragma once

#include <synthrt/SVS/Inference.h>

namespace srt::svs {

    // Inference::Impl - extracted from Inference.cpp (T-P2-07).
    // Minimal PIMPL: stores the InferenceSpec pointer passed at construction.
    class Inference::Impl {
    public:
        const InferenceSpec *m_spec = nullptr;
    };

} // namespace srt::svs
