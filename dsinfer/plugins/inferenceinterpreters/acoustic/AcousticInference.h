#ifndef DSINFER_ACOUSTICINFERENCE_H
#define DSINFER_ACOUSTICINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

#include "AcousticTask.h"

namespace ds {

    class AcousticInference : public Api::Acoustic::L1::AcousticExecInstance {
    public:
        explicit AcousticInference(srt::InferenceSpec &spec);
        ~AcousticInference();

    public:
        srt::Expected<void> initialize(const Api::Acoustic::L1::AcousticInitArgs &args) override;

        srt::Expected<std::unique_ptr<Api::Acoustic::L1::AcousticResult>>
            start(const Api::Acoustic::L1::AcousticStartInput &input) override;
        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Acoustic::L1::AcousticStartInput> input,
                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    private:
        mutable AcousticTask m_task;
    };

}

#endif // DSINFER_ACOUSTICINFERENCE_H
