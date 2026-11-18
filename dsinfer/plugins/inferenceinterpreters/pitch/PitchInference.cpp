#include "PitchInference.h"

#include <utility>

namespace ds {

    namespace Pit = Api::Pitch::L1;

    PitchInference::PitchInference(srt::InferenceSpec &spec) : PitchInference(spec, {}) {
    }

    PitchInference::PitchInference(srt::InferenceSpec &spec, const Pit::ImportOptions &options)
        : Executive(spec), m_task(*this, options) {
    }

    PitchInference::~PitchInference() = default;

    srt::Expected<void> PitchInference::initialize(const Pit::InitArgs &args) {
        return m_task.initialize(args);
    }

    srt::Expected<std::unique_ptr<Pit::Result>>
        PitchInference::start(const Pit::StartInput &input) {
        return m_task.start(input);
    }

    srt::Expected<void> PitchInference::startAsync(std::shared_ptr<const Pit::StartInput> input,
                                                   AsyncCallback callback) {
        return m_task.startAsync(std::move(input), std::move(callback));
    }

    srt::ITask::State PitchInference::state() const noexcept {
        return m_task.state();
    }

    srt::Expected<void> PitchInference::quit() {
        return m_task.stop();
    }

    srt::Expected<void> PitchInference::wait() {
        return m_task.waitForFinished();
    }

}
