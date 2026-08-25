#include "AcousticInference.h"

#include <utility>

namespace ds {

    namespace Ac = Api::Acoustic::L1;

    AcousticInference::AcousticInference(srt::InferenceSpec &spec)
        : AcousticExecutive(spec), m_task(*this) {
    }

    AcousticInference::~AcousticInference() = default;

    srt::Expected<void> AcousticInference::initialize(const Ac::AcousticInitArgs &args) {
        return m_task.initialize(args);
    }

    srt::Expected<std::unique_ptr<Ac::AcousticResult>>
        AcousticInference::start(const Ac::AcousticStartInput &input) {
        return m_task.start(input);
    }

    srt::Expected<void>
        AcousticInference::startAsync(std::shared_ptr<const Ac::AcousticStartInput> input,
                                      AsyncCallback callback) {
        return m_task.startAsync(std::move(input), std::move(callback));
    }

    srt::ITask::State AcousticInference::state() const noexcept {
        return m_task.state();
    }

    srt::Expected<void> AcousticInference::stop() {
        return m_task.stop();
    }

    srt::Expected<void> AcousticInference::waitForFinished() {
        return m_task.waitForFinished();
    }

}
