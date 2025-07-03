#ifndef DSINFER_PITCHINFERENCE_H
#define DSINFER_PITCHINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace ds {

    class PitchInference : public srt::Inference {
    public:
        explicit PitchInference(const srt::InferenceSpec *spec);
        ~PitchInference();

    public:
        srt::Expected<void> initialize(const srt::NO<srt::TaskInitArgs> &args) override;

        srt::Expected<void> start(const srt::NO<srt::TaskStartInput> &input) override;
        srt::Expected<void> startAsync(const srt::NO<srt::TaskStartInput> &input,
                                       const StartAsyncCallback &callback) override;
        bool stop() override;

        srt::NO<srt::TaskResult> result() const override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_PITCHINFERENCE_H