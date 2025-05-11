#ifndef DSINFER_ACOUSTICINFERENCE_H
#define DSINFER_ACOUSTICINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace ds {

    class AcousticInference : public srt::Inference {
    public:
        explicit AcousticInference(const srt::InferenceSpec *spec);
        ~AcousticInference();

    public:
        bool initialize(const srt::NO<srt::TaskInitArgs> &args, srt::Error *error) override;

        bool start(const srt::NO<srt::TaskStartInput> &input, srt::Error *error) override;
        bool startAsync(const srt::NO<srt::TaskStartInput> &input,
                        const StartAsyncCallback &callback, srt::Error *error) override;
        bool stop() override;

        srt::NO<srt::TaskResult> result() const override;
    };

}

#endif // DSINFER_ACOUSTICINFERENCE_H