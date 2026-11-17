#ifndef DSINFER_INFERENCEINTERPRETERS_VARIANCE_VARIANCEINFERENCE_H
#define DSINFER_INFERENCEINTERPRETERS_VARIANCE_VARIANCEINFERENCE_H

#include <memory>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

#include <variance/VarianceTask.h>

namespace ds {

    class VarianceInference : public Api::Variance::L1::Executive {
    public:
        explicit VarianceInference(srt::InferenceSpec &spec);
        ~VarianceInference();

        srt::Expected<void> initialize(const Api::Variance::L1::InitArgs &args) override;
        srt::Expected<std::unique_ptr<Api::Variance::L1::Result>>
            start(const Api::Variance::L1::StartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Variance::L1::StartInput> input,
                                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    private:
        mutable VarianceTask m_task;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_VARIANCE_VARIANCEINFERENCE_H
