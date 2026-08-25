#include "VarianceInference.h"

#include <utility>

namespace ds {

    namespace Var = Api::Variance::L1;

    VarianceInference::VarianceInference(srt::InferenceSpec &spec)
        : VarianceExecInstance(spec), m_task(*this) {
    }

    VarianceInference::~VarianceInference() = default;

    srt::Expected<void> VarianceInference::initialize(const Var::VarianceInitArgs &args) {
        return m_task.initialize(args);
    }

    srt::Expected<std::unique_ptr<Var::VarianceResult>>
        VarianceInference::start(const Var::VarianceStartInput &input) {
        return m_task.start(input);
    }

    srt::Expected<void>
        VarianceInference::startAsync(std::shared_ptr<const Var::VarianceStartInput> input,
                                      AsyncCallback callback) {
        return m_task.startAsync(std::move(input), std::move(callback));
    }

    srt::ITask::State VarianceInference::state() const noexcept {
        return m_task.state();
    }

    srt::Expected<void> VarianceInference::stop() {
        return m_task.stop();
    }

    srt::Expected<void> VarianceInference::waitForFinished() {
        return m_task.waitForFinished();
    }

}
