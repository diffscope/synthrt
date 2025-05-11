#include "AcousticInference.h"

namespace ds {

    AcousticInference::AcousticInference(const srt::InferenceSpec *spec) : Inference(spec) {
    }

    AcousticInference::~AcousticInference() = default;

    bool AcousticInference::initialize(const srt::NO<srt::TaskInitArgs> &args, srt::Error *error) {
        return false;
    }

    bool AcousticInference::start(const srt::NO<srt::TaskStartInput> &input, srt::Error *error) {
        return false;
    }

    bool AcousticInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                       const StartAsyncCallback &callback, srt::Error *error) {
        return false;
    }

    bool AcousticInference::stop() {
        return false;
    }

    srt::NO<srt::TaskResult> AcousticInference::result() const {
        return {};
    }

}