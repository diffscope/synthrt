#ifndef DSINFER_VARIANCEINFERENCE_H
#define DSINFER_VARIANCEINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace ds {

    class VarianceInference : public srt::Inference {
    public:
        explicit VarianceInference(const srt::InferenceSpec *spec);
        ~VarianceInference();

    public:
        srt::Expected<void> initialize(const srt::NO<srt::TaskInitArgs> &args) override;

        srt::Expected<srt::NO<srt::TaskResult>>
            start(const srt::NO<srt::TaskStartInput> &input) override;
        srt::Expected<void> startAsync(const srt::NO<srt::TaskStartInput> &input,
                                       const StartAsyncCallback &callback) override;
        bool stop() override;

        srt::NO<srt::TaskResult> result() const override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_VARIANCEINFERENCE_H