#ifndef DSINFER_DURATIONINFERENCE_H
#define DSINFER_DURATIONINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace srt::svs {

    class DurationInference : public srt::svs::Inference {
    public:
        explicit DurationInference(const srt::svs::InferenceSpec *spec);
        ~DurationInference();

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

#endif // DSINFER_DURATIONINFERENCE_H