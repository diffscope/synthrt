#ifndef DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICINFERENCE_H
#define DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

#include <acoustic/AcousticTask.h>

namespace ds {

    class AcousticInference : public Api::Acoustic::L1::Executive {
    public:
        explicit AcousticInference(srt::InferenceSpec &spec);
        ~AcousticInference();

    public:
        srt::Expected<void> initialize(const Api::Acoustic::L1::InitArgs &args) override;

        srt::Expected<std::unique_ptr<Api::Acoustic::L1::Result>>
            start(const Api::Acoustic::L1::StartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Acoustic::L1::StartInput> input,
                                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;

    protected:
        srt::Expected<void> quit() override;
        srt::Expected<void> wait() override;

    private:
        mutable AcousticTask m_task;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICINFERENCE_H
