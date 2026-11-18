#include "VocoderInference.h"

#include <utility>

namespace ds {

    namespace Voc = Api::Vocoder::L1;

    VocoderInference::VocoderInference(srt::InferenceSpec &spec, const Voc::ImportOptions &,
                                       const Voc::RuntimeOptions &)
        : Executive(spec), m_task(*this) {
    }

    VocoderInference::~VocoderInference() = default;

    srt::Expected<void> VocoderInference::initialize(const Voc::InitArgs &args) {
        return m_task.initialize(args);
    }

    srt::Expected<std::unique_ptr<Voc::Result>>
        VocoderInference::start(const Voc::StartInput &input) {
        return m_task.start(input);
    }

    srt::Expected<void> VocoderInference::startAsync(std::shared_ptr<const Voc::StartInput> input,
                                                     AsyncCallback callback) {
        return m_task.startAsync(std::move(input), std::move(callback));
    }

    srt::ITask::State VocoderInference::state() const noexcept {
        return m_task.state();
    }

    srt::Expected<void> VocoderInference::quit() {
        return m_task.stop();
    }

    srt::Expected<void> VocoderInference::wait() {
        return m_task.waitForFinished();
    }

}
