#include "DiffSingerPipelineExecutive.h"

namespace ds {

    DiffSingerPipelineExecutive::DiffSingerPipelineExecutive(srt::SingerSpec &spec)
        : Api::DiffSinger::L1::DiffSingerPipelineExecutive(spec) {
    }

    DiffSingerPipelineExecutive::~DiffSingerPipelineExecutive() = default;

    srt::Expected<Api::Duration::L1::DurationExecutive *>
        DiffSingerPipelineExecutive::createDuration(
            const Api::Duration::L1::DurationRuntimeOptions &options) {
        auto result = createInference("duration", options, Api::Duration::L1::API_INTERFACE,
                                      Api::Duration::L1::API_VARIANT, Api::Duration::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Duration::L1::DurationExecutive>();
    }

    srt::Expected<Api::Pitch::L1::PitchExecutive *> DiffSingerPipelineExecutive::createPitch(
        const Api::Pitch::L1::PitchRuntimeOptions &options) {
        auto result = createInference("pitch", options, Api::Pitch::L1::API_INTERFACE,
                                      Api::Pitch::L1::API_VARIANT, Api::Pitch::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Pitch::L1::PitchExecutive>();
    }

    srt::Expected<Api::Variance::L1::VarianceExecutive *>
        DiffSingerPipelineExecutive::createVariance(
            const Api::Variance::L1::VarianceRuntimeOptions &options) {
        auto result = createInference("variance", options, Api::Variance::L1::API_INTERFACE,
                                      Api::Variance::L1::API_VARIANT, Api::Variance::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Variance::L1::VarianceExecutive>();
    }

    srt::Expected<Api::Acoustic::L1::AcousticExecutive *>
        DiffSingerPipelineExecutive::createAcoustic(
            const Api::Acoustic::L1::AcousticRuntimeOptions &options) {
        auto result = createInference("acoustic", options, Api::Acoustic::L1::API_INTERFACE,
                                      Api::Acoustic::L1::API_VARIANT, Api::Acoustic::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Acoustic::L1::AcousticExecutive>();
    }

    srt::Expected<Api::Vocoder::L1::VocoderExecutive *> DiffSingerPipelineExecutive::createVocoder(
        const Api::Vocoder::L1::VocoderRuntimeOptions &options) {
        auto result = createInference("vocoder", options, Api::Vocoder::L1::API_INTERFACE,
                                      Api::Vocoder::L1::API_VARIANT, Api::Vocoder::L1::API_LEVEL);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Vocoder::L1::VocoderExecutive>();
    }

    srt::Expected<srt::InferenceExecutive *> DiffSingerPipelineExecutive::createInference(
        std::string_view role, const srt::InferenceRuntimeOptions &options,
        std::string_view expectedInterface, std::string_view expectedVariant, int expectedLevel) {
        auto result = createChild(role, options);
        if (!result) {
            return result.takeError();
        }
        auto executive = *result;
        const auto &executiveSpec = executive->spec();
        if (executiveSpec.interface() != expectedInterface ||
            executiveSpec.variant() != expectedVariant || executiveSpec.level() != expectedLevel) {
            delete executive;
            return srt::Error(srt::Error::InvalidFormat,
                              "inference factory returned an incompatible executive");
        }
        return executive->as<srt::InferenceExecutive>();
    }

}
