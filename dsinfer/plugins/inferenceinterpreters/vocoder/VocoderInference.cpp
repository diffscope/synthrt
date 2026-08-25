#include "VocoderInference.h"

#include <utility>

namespace ds {

    namespace Voc = Api::Vocoder::L1;

    VocoderInference::VocoderInference(srt::InferenceSpec &spec)
        : VocoderExecutive(spec), m_task(*this) {
    }

    VocoderInference::~VocoderInference() = default;

    srt::Expected<void> VocoderInference::initialize(const Voc::VocoderInitArgs &args) {
        return m_task.initialize(args);
    }

    srt::Expected<std::unique_ptr<Voc::VocoderResult>>
        VocoderInference::start(const Voc::VocoderStartInput &input) {
        return m_task.start(input);
    }

    srt::Expected<void>
        VocoderInference::startAsync(std::shared_ptr<const Voc::VocoderStartInput> input,
                                     AsyncCallback callback) {
        return m_task.startAsync(std::move(input), std::move(callback));
    }

    srt::ITask::State VocoderInference::state() const noexcept {
        return m_task.state();
    }

    srt::Expected<void> VocoderInference::stop() {
        return m_task.stop();
    }

    srt::Expected<void> VocoderInference::waitForFinished() {
        return m_task.waitForFinished();
    }

}
