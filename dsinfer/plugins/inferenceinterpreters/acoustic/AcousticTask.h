#ifndef DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICTASK_H
#define DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICTASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

namespace ds {

    class AcousticInference;
    class InferenceDriver;
    class InferenceSession;

    /// Executes one acoustic inference model behind its typed executive.
    class AcousticTask : public srt::ITask {
    public:
        using ITask::start;
        using ITask::startAsync;

        explicit AcousticTask(AcousticInference &inference);
        ~AcousticTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;

        srt::Expected<void> initialize(const Api::Acoustic::L1::InitArgs &args);
        srt::Expected<std::unique_ptr<Api::Acoustic::L1::Result>>
            start(const Api::Acoustic::L1::StartInput &input);
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Acoustic::L1::StartInput> input,
                                       Api::Acoustic::L1::Executive::AsyncCallback callback);

        void updateState(State state) noexcept;

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &token) override;

        srt::Expected<void> quit() override;

        srt::Expected<void> wait() override;

        State classifyOutcome(const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome,
                              bool stopRequested) noexcept override;

    private:
        srt::Expected<std::unique_ptr<Api::Acoustic::L1::Result>>
            execute(const Api::Acoustic::L1::StartInput &input);

        AcousticInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_session;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICTASK_H
