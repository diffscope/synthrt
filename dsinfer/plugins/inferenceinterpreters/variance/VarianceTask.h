#ifndef DSINFER_VARIANCETASK_H
#define DSINFER_VARIANCETASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

namespace ds {

    class InferenceDriver;
    class InferenceSession;
    class VarianceInference;

    /// Executes one variance inference model behind its typed executive.
    class VarianceTask : public srt::ITask {
    public:
        explicit VarianceTask(VarianceInference &inference);
        ~VarianceTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       srt::ITask::AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

        srt::Expected<void> initialize(const Api::Variance::L1::VarianceInitArgs &args);
        srt::Expected<std::unique_ptr<Api::Variance::L1::VarianceResult>>
            start(const Api::Variance::L1::VarianceStartInput &input);
        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Variance::L1::VarianceStartInput> input,
                       Api::Variance::L1::VarianceExecutive::AsyncCallback callback);

    private:
        VarianceInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_encoderSession;
        std::unique_ptr<InferenceSession> m_predictorSession;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_VARIANCETASK_H
