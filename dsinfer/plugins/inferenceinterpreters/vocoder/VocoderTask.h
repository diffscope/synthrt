#ifndef DSINFER_INFERENCEINTERPRETERS_VOCODER_VOCODERTASK_H
#define DSINFER_INFERENCEINTERPRETERS_VOCODER_VOCODERTASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds {

    class InferenceDriver;
    class InferenceSession;
    class VocoderInference;

    /// Executes one vocoder inference model behind its typed executive.
    class VocoderTask : public srt::ITask {
    public:
        using ITask::start;
        using ITask::startAsync;

        explicit VocoderTask(VocoderInference &inference);
        ~VocoderTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;

        srt::Expected<void> initialize(const Api::Vocoder::L1::InitArgs &args);
        srt::Expected<std::unique_ptr<Api::Vocoder::L1::Result>>
            start(const Api::Vocoder::L1::StartInput &input);
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Vocoder::L1::StartInput> input,
                                       Api::Vocoder::L1::Executive::AsyncCallback callback);

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &token) override;

        srt::Expected<void> quit() override;

        srt::Expected<void> wait() override;

        State classifyOutcome(const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome,
                              bool stopRequested) noexcept override;

    private:
        VocoderInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_session;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_VOCODER_VOCODERTASK_H
