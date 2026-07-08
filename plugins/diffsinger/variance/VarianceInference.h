#ifndef DSINFER_VARIANCEINFERENCE_H
#define DSINFER_VARIANCEINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace srt::svs {

    class VarianceInference : public srt::svs::Inference {
    public:
        explicit VarianceInference(const srt::svs::InferenceSpec *spec);
        ~VarianceInference();

    public:
        srt::core::Expected<void> initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) override;

        srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
            start(const srt::core::NO<srt::core::TaskStartInput> &input) override;
        srt::core::Expected<void> startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                       const StartAsyncCallback &callback) override;
        bool stop() override;

        srt::core::NO<srt::core::TaskResult> result() const override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_VARIANCEINFERENCE_H