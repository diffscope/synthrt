#include "VocoderInference.h"

#include <stdcorelib/pimpl.h>

#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds {

    using namespace Api::Vocoder::L1;

    class VocoderInference::Impl {
    public:
        srt::NO<VocoderResult> result;
    };

    VocoderInference::VocoderInference(const srt::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    VocoderInference::~VocoderInference() = default;

    bool VocoderInference::initialize(const srt::NO<srt::TaskInitArgs> &args, srt::Error *error) {
        return false;
    }

    bool VocoderInference::start(const srt::NO<srt::TaskStartInput> &input, srt::Error *error) {
        __stdc_impl_t;
        // TODO:
        setState(Running); // 设置状态

        // ...

        setState(Idle);
        impl.result = srt::NO<VocoderResult>::create(); // 创建结果
        return false;
    }

    bool VocoderInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                       const StartAsyncCallback &callback, srt::Error *error) {
        // TODO:
        return false;
    }

    bool VocoderInference::stop() {
        // TODO:
        setState(Terminated); // 设置状态
        return false;
    }

    srt::NO<srt::TaskResult> VocoderInference::result() const {
        __stdc_impl_t;
        return impl.result;
    }

}