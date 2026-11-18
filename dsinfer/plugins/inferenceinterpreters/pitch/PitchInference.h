#ifndef DSINFER_INFERENCEINTERPRETERS_PITCH_PITCHINFERENCE_H
#define DSINFER_INFERENCEINTERPRETERS_PITCH_PITCHINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

#include <pitch/PitchTask.h>

namespace ds {

    class PitchInference : public Api::Pitch::L1::Executive {
    public:
        PitchInference(srt::InferenceSpec &spec, const Api::Pitch::L1::ImportOptions &importOptions,
                       const Api::Pitch::L1::RuntimeOptions &runtimeOptions);
        ~PitchInference();

        srt::Expected<void> initialize(const Api::Pitch::L1::InitArgs &args) override;
        srt::Expected<std::unique_ptr<Api::Pitch::L1::Result>>
            start(const Api::Pitch::L1::StartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Pitch::L1::StartInput> input,
                                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;

    protected:
        srt::Expected<void> quit() override;
        srt::Expected<void> wait() override;

    private:
        mutable PitchTask m_task;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_PITCH_PITCHINFERENCE_H
