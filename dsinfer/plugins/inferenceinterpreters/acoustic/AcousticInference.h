#ifndef DSINFER_ACOUSTICINFERENCE_H
#define DSINFER_ACOUSTICINFERENCE_H

#include <synthrt/SVS/Inference.h>

namespace ds {

    class AcousticInference : public srt::Inference {
    public:
        explicit AcousticInference(srt::InferenceSpec &spec);
        ~AcousticInference();

    public:
        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;

        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_ACOUSTICINFERENCE_H
