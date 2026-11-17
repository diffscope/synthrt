#include "DiffSingerPipelineExecutive.h"

namespace ds {

    DiffSingerPipelineExecutive::DiffSingerPipelineExecutive(srt::SingerSpec &spec)
        : Api::DiffSinger::L1::PipelineExecutive(spec) {
    }

    DiffSingerPipelineExecutive::~DiffSingerPipelineExecutive() = default;

    srt::Expected<Api::Duration::L1::Executive *> DiffSingerPipelineExecutive::createDuration(
        const Api::Duration::L1::RuntimeOptions &options) {
        auto result = createInference("singer/duration", options, Api::Duration::L1::Contract);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Duration::L1::Executive>();
    }

    srt::Expected<Api::Pitch::L1::Executive *>
        DiffSingerPipelineExecutive::createPitch(const Api::Pitch::L1::RuntimeOptions &options) {
        auto result = createInference("singer/pitch", options, Api::Pitch::L1::Contract);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Pitch::L1::Executive>();
    }

    srt::Expected<Api::Variance::L1::Executive *> DiffSingerPipelineExecutive::createVariance(
        const Api::Variance::L1::RuntimeOptions &options) {
        auto result = createInference("singer/variance", options, Api::Variance::L1::Contract);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Variance::L1::Executive>();
    }

    srt::Expected<Api::Acoustic::L1::Executive *> DiffSingerPipelineExecutive::createAcoustic(
        const Api::Acoustic::L1::RuntimeOptions &options) {
        auto result = createInference("singer/acoustic", options, Api::Acoustic::L1::Contract);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Acoustic::L1::Executive>();
    }

    srt::Expected<Api::Vocoder::L1::Executive *> DiffSingerPipelineExecutive::createVocoder(
        const Api::Vocoder::L1::RuntimeOptions &options) {
        auto result = createInference("singer/vocoder", options, Api::Vocoder::L1::Contract);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Vocoder::L1::Executive>();
    }

    srt::Expected<srt::InferenceExecutive *>
        DiffSingerPipelineExecutive::createInference(std::string_view role,
                                                     const srt::InferenceRuntimeOptions &options,
                                                     const srt::ContribContract &expected) {
        auto result = createChild(role, options);
        if (!result) {
            return result.takeError();
        }
        auto executive = *result;
        const auto &executiveSpec = executive->spec();
        if (executiveSpec.triplet().contract() != expected) {
            delete executive;
            return srt::Error(srt::Error::InvalidFormat,
                              "inference factory returned an incompatible executive");
        }
        return executive->as<srt::InferenceExecutive>();
    }

}
