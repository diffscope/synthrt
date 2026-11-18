#ifndef DSINFER_INFERENCEINTERPRETERS_DURATION_DURATIONINFERENCE_H
#define DSINFER_INFERENCEINTERPRETERS_DURATION_DURATIONINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

#include <duration/DurationTask.h>

namespace ds {

    class DurationInference : public Api::Duration::L1::Executive {
    public:
        DurationInference(srt::InferenceSpec &spec,
                          const Api::Duration::L1::ImportOptions &importOptions,
                          const Api::Duration::L1::RuntimeOptions &runtimeOptions);
        ~DurationInference();

        srt::Expected<void> initialize(const Api::Duration::L1::InitArgs &args) override;
        srt::Expected<std::unique_ptr<Api::Duration::L1::Result>>
            start(const Api::Duration::L1::StartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Duration::L1::StartInput> input,
                                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;

    protected:
        srt::Expected<void> quit() override;
        srt::Expected<void> wait() override;

    private:
        mutable DurationTask m_task;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_DURATION_DURATIONINFERENCE_H
