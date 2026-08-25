#ifndef DSINFER_DURATIONTASK_H
#define DSINFER_DURATIONTASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

namespace ds {

    class DurationInference;
    class InferenceDriver;
    class InferenceSession;

    /// Executes one duration inference model behind its typed execution instance.
    class DurationTask : public srt::ITask {
    public:
        explicit DurationTask(DurationInference &inference);
        ~DurationTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       srt::ITask::AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

        srt::Expected<void> initialize(const Api::Duration::L1::DurationInitArgs &args);
        srt::Expected<std::unique_ptr<Api::Duration::L1::DurationResult>>
            start(const Api::Duration::L1::DurationStartInput &input);
        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Duration::L1::DurationStartInput> input,
                       Api::Duration::L1::DurationExecInstance::AsyncCallback callback);

    private:
        DurationInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_encoderSession;
        std::unique_ptr<InferenceSession> m_predictorSession;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_DURATIONTASK_H
