#ifndef DSINFER_INFERENCEINTERPRETERS_DURATION_DURATIONTASK_H
#define DSINFER_INFERENCEINTERPRETERS_DURATION_DURATIONTASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

namespace ds {

    class DurationInference;
    class InferenceDriver;
    class InferenceSession;

    /// Executes one duration inference model behind its typed executive.
    class DurationTask : public srt::ITask {
    public:
        using ITask::start;
        using ITask::startAsync;

        explicit DurationTask(DurationInference &inference);
        ~DurationTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;

        srt::Expected<void> initialize(const Api::Duration::L1::InitArgs &args);
        srt::Expected<std::unique_ptr<Api::Duration::L1::Result>>
            start(const Api::Duration::L1::StartInput &input);
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Duration::L1::StartInput> input,
                                       Api::Duration::L1::Executive::AsyncCallback callback);

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &token) override;

        srt::Expected<void> quit() override;

        srt::Expected<void> wait() override;

        State classifyOutcome(const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome,
                              bool stopRequested) noexcept override;

    private:
        DurationInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_encoderSession;
        std::unique_ptr<InferenceSession> m_predictorSession;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_DURATION_DURATIONTASK_H
