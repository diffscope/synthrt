#ifndef DSINFER_VOCODERINFERENCE_H
#define DSINFER_VOCODERINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include "VocoderTask.h"

namespace ds {

    class VocoderInference : public Api::Vocoder::L1::VocoderExecutive {
    public:
        explicit VocoderInference(srt::InferenceSpec &spec);
        ~VocoderInference();

        srt::Expected<void> initialize(const Api::Vocoder::L1::VocoderInitArgs &args) override;
        srt::Expected<std::unique_ptr<Api::Vocoder::L1::VocoderResult>>
            start(const Api::Vocoder::L1::VocoderStartInput &input) override;
        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Vocoder::L1::VocoderStartInput> input,
                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    private:
        mutable VocoderTask m_task;
    };

}

#endif // DSINFER_VOCODERINFERENCE_H
