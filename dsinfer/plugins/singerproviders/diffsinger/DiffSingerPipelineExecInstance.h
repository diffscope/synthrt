#ifndef DSINFER_DIFFSINGERPIPELINEEXECINSTANCE_H
#define DSINFER_DIFFSINGERPIPELINEEXECINSTANCE_H

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds {

    class DiffSingerPipelineExecInstance
        : public Api::DiffSinger::L1::DiffSingerPipelineExecInstance {
    public:
        explicit DiffSingerPipelineExecInstance(srt::SingerSpec &spec);
        ~DiffSingerPipelineExecInstance();

    public:
        srt::Expected<Api::Duration::L1::DurationExecInstance *>
            createDuration(const Api::Duration::L1::DurationRuntimeOptions &options) override;

        srt::Expected<Api::Pitch::L1::PitchExecInstance *>
            createPitch(const Api::Pitch::L1::PitchRuntimeOptions &options) override;

        srt::Expected<Api::Variance::L1::VarianceExecInstance *>
            createVariance(const Api::Variance::L1::VarianceRuntimeOptions &options) override;

        srt::Expected<Api::Acoustic::L1::AcousticExecInstance *>
            createAcoustic(const Api::Acoustic::L1::AcousticRuntimeOptions &options) override;

        srt::Expected<Api::Vocoder::L1::VocoderExecInstance *>
            createVocoder(const Api::Vocoder::L1::VocoderRuntimeOptions &options) override;
    };

}

#endif // DSINFER_DIFFSINGERPIPELINEEXECINSTANCE_H
