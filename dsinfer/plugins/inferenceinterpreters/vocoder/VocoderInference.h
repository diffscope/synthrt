#ifndef DSINFER_VOCODERINFERENCE_H
#define DSINFER_VOCODERINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace ds {

    class VocoderInference : public srt::Inference {
    public:
        explicit VocoderInference(const srt::InferenceSpec *spec);
        ~VocoderInference();

    public:
        bool initialize(const srt::NO<srt::TaskInitArgs> &args, srt::Error *error) override;

        bool start(const srt::NO<srt::TaskStartInput> &input, srt::Error *error) override;
        bool startAsync(const srt::NO<srt::TaskStartInput> &input,
                        const StartAsyncCallback &callback, srt::Error *error) override;
        bool stop() override;

        srt::NO<srt::TaskResult> result() const override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_VOCODERINFERENCE_H