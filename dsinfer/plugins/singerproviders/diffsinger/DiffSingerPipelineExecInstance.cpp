#include "DiffSingerPipelineExecInstance.h"

namespace ds {

    DiffSingerPipelineExecInstance::DiffSingerPipelineExecInstance(srt::SingerSpec &spec)
        : Api::DiffSinger::L1::DiffSingerPipelineExecInstance(spec) {
    }

    DiffSingerPipelineExecInstance::~DiffSingerPipelineExecInstance() = default;

    srt::Expected<Api::Duration::L1::DurationExecInstance *>
        DiffSingerPipelineExecInstance::createDuration(
            const Api::Duration::L1::DurationRuntimeOptions &options) {
        auto result = createInference("duration", options, Api::Duration::L1::API_INTERFACE,
                                      Api::Duration::L1::API_VARIANT, Api::Duration::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Duration::L1::DurationExecInstance>();
    }

    srt::Expected<Api::Pitch::L1::PitchExecInstance *> DiffSingerPipelineExecInstance::createPitch(
        const Api::Pitch::L1::PitchRuntimeOptions &options) {
        auto result = createInference("pitch", options, Api::Pitch::L1::API_INTERFACE,
                                      Api::Pitch::L1::API_VARIANT, Api::Pitch::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Pitch::L1::PitchExecInstance>();
    }

    srt::Expected<Api::Variance::L1::VarianceExecInstance *>
        DiffSingerPipelineExecInstance::createVariance(
            const Api::Variance::L1::VarianceRuntimeOptions &options) {
        auto result = createInference("variance", options, Api::Variance::L1::API_INTERFACE,
                                      Api::Variance::L1::API_VARIANT, Api::Variance::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Variance::L1::VarianceExecInstance>();
    }

    srt::Expected<Api::Acoustic::L1::AcousticExecInstance *>
        DiffSingerPipelineExecInstance::createAcoustic(
            const Api::Acoustic::L1::AcousticRuntimeOptions &options) {
        auto result = createInference("acoustic", options, Api::Acoustic::L1::API_INTERFACE,
                                      Api::Acoustic::L1::API_VARIANT, Api::Acoustic::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Acoustic::L1::AcousticExecInstance>();
    }

    srt::Expected<Api::Vocoder::L1::VocoderExecInstance *>
        DiffSingerPipelineExecInstance::createVocoder(
            const Api::Vocoder::L1::VocoderRuntimeOptions &options) {
        auto result = createInference("vocoder", options, Api::Vocoder::L1::API_INTERFACE,
                                      Api::Vocoder::L1::API_VARIANT, Api::Vocoder::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Vocoder::L1::VocoderExecInstance>();
    }

    srt::Expected<srt::InferenceExecInstance *> DiffSingerPipelineExecInstance::createInference(
        std::string_view role, const srt::InferenceRuntimeOptions &options,
        std::string_view expectedInterface, std::string_view expectedVariant, int expectedLevel) {
        auto result = createChild(role, options);
        if (!result) {
            return result.takeError();
        }
        auto *instance = *result;
        const auto &instanceSpec = instance->spec();
        if (instanceSpec.interface() != expectedInterface ||
            instanceSpec.variant() != expectedVariant || instanceSpec.level() != expectedLevel) {
            delete instance;
            return srt::Error(srt::Error::InvalidFormat,
                              "inference factory returned an incompatible execution instance");
        }
        return instance->as<srt::InferenceExecInstance>();
    }

}
