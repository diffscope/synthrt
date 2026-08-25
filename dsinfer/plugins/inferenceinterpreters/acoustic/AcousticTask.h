#ifndef DSINFER_ACOUSTICTASK_H
#define DSINFER_ACOUSTICTASK_H

#include <memory>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

namespace ds {

    class AcousticInference;
    class InferenceDriver;
    class InferenceSession;

    /// Executes one acoustic inference model behind its typed execution instance.
    class AcousticTask : public srt::ITask {
    public:
        explicit AcousticTask(AcousticInference &inference);
        ~AcousticTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       srt::ITask::AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

        srt::Expected<void> initialize(const Api::Acoustic::L1::AcousticInitArgs &args);
        srt::Expected<std::unique_ptr<Api::Acoustic::L1::AcousticResult>>
            start(const Api::Acoustic::L1::AcousticStartInput &input);
        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Acoustic::L1::AcousticStartInput> input,
                       Api::Acoustic::L1::AcousticExecInstance::AsyncCallback callback);

        void updateState(State state) noexcept;

    private:
        AcousticInference *m_inference;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_session;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_ACOUSTICTASK_H
