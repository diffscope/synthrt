#ifndef DSINFER_VOCODERINFERENCE_H
#define DSINFER_VOCODERINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace ds {

    class VocoderInference : public srt::Inference {
    public:
        explicit VocoderInference(const srt::InferenceSpec *spec);
        ~VocoderInference();

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

#endif // DSINFER_VOCODERINFERENCE_H