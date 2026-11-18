#ifndef DSINFER_INFERENCEINTERPRETERS_VARIANCE_VARIANCETASK_H
#define DSINFER_INFERENCEINTERPRETERS_VARIANCE_VARIANCETASK_H

#include <map>
#include <memory>
#include <set>
#include <shared_mutex>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

namespace ds {

    class InferenceDriver;
    class InferenceSession;
    class VarianceInference;

    /// Executes one variance inference model behind its typed executive.
    class VarianceTask : public srt::ITask {
    public:
        using ITask::start;
        using ITask::startAsync;

        explicit VarianceTask(VarianceInference &inference);
        VarianceTask(VarianceInference &inference, const Api::Variance::L1::ImportOptions &options);
        ~VarianceTask();

        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;

        srt::Expected<void> initialize(const Api::Variance::L1::InitArgs &args);
        srt::Expected<std::unique_ptr<Api::Variance::L1::Result>>
            start(const Api::Variance::L1::StartInput &input);
        srt::Expected<void> startAsync(std::shared_ptr<const Api::Variance::L1::StartInput> input,
                                       Api::Variance::L1::Executive::AsyncCallback callback);

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &token) override;

        srt::Expected<void> quit() override;

        srt::Expected<void> wait() override;

        State classifyOutcome(const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome,
                              bool stopRequested) noexcept override;

    private:
        VarianceInference *m_inference;
        std::map<std::string, std::string> m_speakerMapping;
        std::set<ParamTag> m_predictions;
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_encoderSession;
        std::unique_ptr<InferenceSession> m_predictorSession;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_VARIANCE_VARIANCETASK_H
