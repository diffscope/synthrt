#ifndef DSINFER_INFERENCEINTERPRETERS_VOCODER_VOCODERINFERENCE_H
#define DSINFER_INFERENCEINTERPRETERS_VOCODER_VOCODERINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include <vocoder/VocoderTask.h>

namespace ds {

    class VocoderInference : public Api::Vocoder::L1::Executive {
    public:
        explicit VocoderInference(srt::InferenceSpec &spec);
        ~VocoderInference();

        srt::Expected<void> initialize(const Api::Vocoder::L1::InitArgs &args) override;
        srt::Expected<std::unique_ptr<Api::Vocoder::L1::Result>>
            start(const Api::Vocoder::L1::StartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Vocoder::L1::StartInput> input,
                                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;

    protected:
        srt::Expected<void> quit() override;
        srt::Expected<void> wait() override;

    private:
        mutable VocoderTask m_task;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_VOCODER_VOCODERINFERENCE_H
