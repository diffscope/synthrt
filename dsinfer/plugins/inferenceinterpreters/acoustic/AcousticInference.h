#ifndef DSINFER_ACOUSTICINFERENCE_H
#define DSINFER_ACOUSTICINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace ds {

    class AcousticInference : public srt::Inference {
    public:
        explicit AcousticInference(const srt::InferenceSpec *spec);
        ~AcousticInference();

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

#endif // DSINFER_ACOUSTICINFERENCE_H