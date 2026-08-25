#include "PitchInference.h"

#include <utility>

namespace ds {

    namespace Pit = Api::Pitch::L1;

    PitchInference::PitchInference(srt::InferenceSpec &spec)
        : PitchExecInstance(spec), m_task(*this) {
    }

    PitchInference::~PitchInference() = default;

    srt::Expected<void> PitchInference::initialize(const Pit::PitchInitArgs &args) {
        return m_task.initialize(args);
    }

    srt::Expected<std::unique_ptr<Pit::PitchResult>>
        PitchInference::start(const Pit::PitchStartInput &input) {
        return m_task.start(input);
    }

    srt::Expected<void>
        PitchInference::startAsync(std::shared_ptr<const Pit::PitchStartInput> input,
                                   AsyncCallback callback) {
        return m_task.startAsync(std::move(input), std::move(callback));
    }

    srt::ITask::State PitchInference::state() const noexcept {
        return m_task.state();
    }

    srt::Expected<void> PitchInference::stop() {
        return m_task.stop();
    }

    srt::Expected<void> PitchInference::waitForFinished() {
        return m_task.waitForFinished();
    }

}
