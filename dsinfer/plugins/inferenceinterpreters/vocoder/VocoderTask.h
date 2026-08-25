#ifndef DSINFER_VOCODERTASK_H
#define DSINFER_VOCODERTASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds {

    class InferenceDriver;
    class InferenceSession;
    class VocoderInference;

    /// Executes one vocoder inference model behind its typed execution instance.
    class VocoderTask : public srt::ITask {
    public:
        explicit VocoderTask(VocoderInference &inference);
        ~VocoderTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       srt::ITask::AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

        srt::Expected<void> initialize(const Api::Vocoder::L1::VocoderInitArgs &args);
        srt::Expected<std::unique_ptr<Api::Vocoder::L1::VocoderResult>>
            start(const Api::Vocoder::L1::VocoderStartInput &input);
        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Vocoder::L1::VocoderStartInput> input,
                       Api::Vocoder::L1::VocoderExecInstance::AsyncCallback callback);

    private:
        VocoderInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_session;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_VOCODERTASK_H
