#ifndef DSINFER_PITCHINFERENCE_H
#define DSINFER_PITCHINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

#include "PitchTask.h"

namespace ds {

    class PitchInference : public Api::Pitch::L1::PitchExecutive {
    public:
        explicit PitchInference(srt::InferenceSpec &spec);
        ~PitchInference();

        srt::Expected<void> initialize(const Api::Pitch::L1::PitchInitArgs &args) override;
        srt::Expected<std::unique_ptr<Api::Pitch::L1::PitchResult>>
            start(const Api::Pitch::L1::PitchStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Pitch::L1::PitchStartInput> input,
                                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    private:
        mutable PitchTask m_task;
    };

}

#endif // DSINFER_PITCHINFERENCE_H
