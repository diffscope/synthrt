#ifndef DSINFER_INFERENCEINTERPRETERS_PITCH_PITCHTASK_H
#define DSINFER_INFERENCEINTERPRETERS_PITCH_PITCHTASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

namespace ds {

    class InferenceDriver;
    class InferenceSession;
    class PitchInference;

    /// Executes one pitch inference model behind its typed executive.
    class PitchTask : public srt::ITask {
    public:
        using ITask::start;
        using ITask::startAsync;

        explicit PitchTask(PitchInference &inference);
        ~PitchTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;

        srt::Expected<void> initialize(const Api::Pitch::L1::InitArgs &args);
        srt::Expected<std::unique_ptr<Api::Pitch::L1::Result>>
            start(const Api::Pitch::L1::StartInput &input);
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Pitch::L1::StartInput> input,
                                       Api::Pitch::L1::Executive::AsyncCallback callback);

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &token) override;

        srt::Expected<void> quit() override;

        srt::Expected<void> wait() override;

        State classifyOutcome(const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome,
                              bool stopRequested) noexcept override;

    private:
        PitchInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_encoderSession;
        std::unique_ptr<InferenceSession> m_predictorSession;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_PITCH_PITCHTASK_H
