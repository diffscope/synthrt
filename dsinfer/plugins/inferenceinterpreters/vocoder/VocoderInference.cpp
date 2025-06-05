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

    srt::Expected<void> VocoderInference::initialize(const srt::NO<srt::TaskInitArgs> &args) {
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> VocoderInference::start(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;
        // TODO:
        setState(Running); // 设置状态

        // ...

        setState(Idle);
        impl.result = srt::NO<VocoderResult>::create(); // 创建结果
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> VocoderInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                     const StartAsyncCallback &callback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
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