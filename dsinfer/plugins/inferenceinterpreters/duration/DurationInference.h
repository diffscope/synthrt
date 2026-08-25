#ifndef DSINFER_DURATIONINFERENCE_H
#define DSINFER_DURATIONINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

#include "DurationTask.h"

namespace ds {

    class DurationInference : public Api::Duration::L1::DurationExecutive {
    public:
        explicit DurationInference(srt::InferenceSpec &spec);
        ~DurationInference();

        srt::Expected<void> initialize(const Api::Duration::L1::DurationInitArgs &args) override;
        srt::Expected<std::unique_ptr<Api::Duration::L1::DurationResult>>
            start(const Api::Duration::L1::DurationStartInput &input) override;
        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Duration::L1::DurationStartInput> input,
                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    private:
        mutable DurationTask m_task;
    };

}

#endif // DSINFER_DURATIONINFERENCE_H
