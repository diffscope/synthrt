#include "DurationInference.h"

#include <utility>

namespace ds {

    namespace Dur = Api::Duration::L1;

    DurationInference::DurationInference(srt::InferenceSpec &spec)
        : Executive(spec), m_task(*this) {
    }

    DurationInference::~DurationInference() = default;

    srt::Expected<void> DurationInference::initialize(const Dur::InitArgs &args) {
        return m_task.initialize(args);
    }

    srt::Expected<std::unique_ptr<Dur::Result>>
        DurationInference::start(const Dur::StartInput &input) {
        return m_task.start(input);
    }

    srt::Expected<void> DurationInference::startAsync(std::shared_ptr<const Dur::StartInput> input,
                                                      AsyncCallback callback) {
        return m_task.startAsync(std::move(input), std::move(callback));
    }

    srt::ITask::State DurationInference::state() const noexcept {
        return m_task.state();
    }

    srt::Expected<void> DurationInference::quit() {
        return m_task.stop();
    }

    srt::Expected<void> DurationInference::wait() {
        return m_task.waitForFinished();
    }

}
