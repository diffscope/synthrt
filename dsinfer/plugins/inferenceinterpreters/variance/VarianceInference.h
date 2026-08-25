#ifndef DSINFER_VARIANCEINFERENCE_H
#define DSINFER_VARIANCEINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

#include "VarianceTask.h"

namespace ds {

    class VarianceInference : public Api::Variance::L1::VarianceExecInstance {
    public:
        explicit VarianceInference(srt::InferenceSpec &spec);
        ~VarianceInference();

        srt::Expected<void> initialize(const Api::Variance::L1::VarianceInitArgs &args) override;
        srt::Expected<std::unique_ptr<Api::Variance::L1::VarianceResult>>
            start(const Api::Variance::L1::VarianceStartInput &input) override;
        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Variance::L1::VarianceStartInput> input,
                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    private:
        mutable VarianceTask m_task;
    };

}

#endif // DSINFER_VARIANCEINFERENCE_H
