#include "DiffSingerPipelineExecInstance.h"

namespace ds {

    DiffSingerPipelineExecInstance::DiffSingerPipelineExecInstance(srt::SingerSpec &spec)
        : Api::DiffSinger::L1::DiffSingerPipelineExecInstance(spec) {
    }

    DiffSingerPipelineExecInstance::~DiffSingerPipelineExecInstance() = default;

    srt::Expected<Api::Duration::L1::DurationExecInstance *>
        DiffSingerPipelineExecInstance::createDuration(
            const Api::Duration::L1::DurationRuntimeOptions &options) {
        auto result = createChild("duration", options);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Duration::L1::DurationExecInstance>();
    }

    srt::Expected<Api::Pitch::L1::PitchExecInstance *> DiffSingerPipelineExecInstance::createPitch(
        const Api::Pitch::L1::PitchRuntimeOptions &options) {
        auto result = createChild("pitch", options);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Pitch::L1::PitchExecInstance>();
    }

    srt::Expected<Api::Variance::L1::VarianceExecInstance *>
        DiffSingerPipelineExecInstance::createVariance(
            const Api::Variance::L1::VarianceRuntimeOptions &options) {
        auto result = createChild("variance", options);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Variance::L1::VarianceExecInstance>();
    }

    srt::Expected<Api::Acoustic::L1::AcousticExecInstance *>
        DiffSingerPipelineExecInstance::createAcoustic(
            const Api::Acoustic::L1::AcousticRuntimeOptions &options) {
        auto result = createChild("acoustic", options);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Acoustic::L1::AcousticExecInstance>();
    }

    srt::Expected<Api::Vocoder::L1::VocoderExecInstance *>
        DiffSingerPipelineExecInstance::createVocoder(
            const Api::Vocoder::L1::VocoderRuntimeOptions &options) {
        auto result = createChild("vocoder", options);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Vocoder::L1::VocoderExecInstance>();
    }

}
