#ifndef DSINFER_PITCHTASK_H
#define DSINFER_PITCHTASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

namespace ds {

    class InferenceDriver;
    class InferenceSession;
    class PitchInference;

    /// Executes one pitch inference model behind its typed execution instance.
    class PitchTask : public srt::ITask {
    public:
        explicit PitchTask(PitchInference &inference);
        ~PitchTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       srt::ITask::AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

        srt::Expected<void> initialize(const Api::Pitch::L1::PitchInitArgs &args);
        srt::Expected<std::unique_ptr<Api::Pitch::L1::PitchResult>>
            start(const Api::Pitch::L1::PitchStartInput &input);
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Pitch::L1::PitchStartInput> input,
                                       Api::Pitch::L1::PitchExecInstance::AsyncCallback callback);

    private:
        PitchInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_encoderSession;
        std::unique_ptr<InferenceSession> m_predictorSession;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_PITCHTASK_H
