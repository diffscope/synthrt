#ifndef DSINFER_DIFFSINGERPIPELINEEXECUTIVE_H
#define DSINFER_DIFFSINGERPIPELINEEXECUTIVE_H

#include <string_view>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds {

    class DiffSingerPipelineExecutive : public Api::DiffSinger::L1::DiffSingerPipelineExecutive {
    public:
        explicit DiffSingerPipelineExecutive(srt::SingerSpec &spec);
        ~DiffSingerPipelineExecutive();

    public:
        srt::Expected<Api::Duration::L1::DurationExecutive *>
            createDuration(const Api::Duration::L1::DurationRuntimeOptions &options) override;

        srt::Expected<Api::Pitch::L1::PitchExecutive *>
            createPitch(const Api::Pitch::L1::PitchRuntimeOptions &options) override;

        srt::Expected<Api::Variance::L1::VarianceExecutive *>
            createVariance(const Api::Variance::L1::VarianceRuntimeOptions &options) override;

        srt::Expected<Api::Acoustic::L1::AcousticExecutive *>
            createAcoustic(const Api::Acoustic::L1::AcousticRuntimeOptions &options) override;

        srt::Expected<Api::Vocoder::L1::VocoderExecutive *>
            createVocoder(const Api::Vocoder::L1::VocoderRuntimeOptions &options) override;

    private:
        srt::Expected<srt::InferenceExecutive *>
            createInference(std::string_view role, const srt::InferenceRuntimeOptions &options,
                            std::string_view expectedInterface, std::string_view expectedVariant,
                            int expectedLevel);
    };

}

#endif // DSINFER_DIFFSINGERPIPELINEEXECUTIVE_H
