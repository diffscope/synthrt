#ifndef DSINFER_VARIANCEINFERENCE_H
#define DSINFER_VARIANCEINFERENCE_H

#include <memory>
#include <shared_mutex>

#include <synthrt/SVS/InferenceExecInstance.h>

namespace ds {

    class InferenceDriver;
    class InferenceSession;

    class VarianceInference : public srt::InferenceExecInstance {
    public:
        explicit VarianceInference(srt::InferenceSpec &spec);
        ~VarianceInference();

    public:
        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;

        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    private:
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_encoderSession;
        std::unique_ptr<InferenceSession> m_predictorSession;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_VARIANCEINFERENCE_H
