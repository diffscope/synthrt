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

    bool AcousticInference::initialize(const srt::NO<srt::TaskInitArgs> &args, srt::Error *error) {
        return false;
    }

    bool AcousticInference::start(const srt::NO<srt::TaskStartInput> &input, srt::Error *error) {
        __stdc_impl_t;
        // TODO:
        setState(Running); // 设置状态

        // ...

        setState(Idle);
        impl.result = srt::NO<AcousticResult>::create(); // 创建结果
        return false;
    }

    bool AcousticInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                       const StartAsyncCallback &callback, srt::Error *error) {
        // TODO:
        return false;
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