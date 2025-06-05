#include "AcousticInference.h"

#include <stdcorelib/pimpl.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

namespace ds {

    using namespace Api::Acoustic::L1;

    class AcousticInference::Impl {
    public:
        srt::NO<AcousticResult> result;
    };

    AcousticInference::AcousticInference(const srt::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    AcousticInference::~AcousticInference() = default;

    srt::Expected<void> AcousticInference::initialize(const srt::NO<srt::TaskInitArgs> &args) {
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> AcousticInference::start(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;
        // TODO:
        setState(Running); // 设置状态

        // ...

        setState(Idle);
        impl.result = srt::NO<AcousticResult>::create(); // 创建结果
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> AcousticInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                      const StartAsyncCallback &callback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
    }

    bool AcousticInference::stop() {
        // TODO:
        setState(Terminated); // 设置状态
        return false;
    }

    srt::NO<srt::TaskResult> AcousticInference::result() const {
        __stdc_impl_t;
        return impl.result;
    }

}