#ifndef DSINFER_SINGERPROVIDERS_DIFFSINGER_DIFFSINGERPIPELINEEXECUTIVE_H
#define DSINFER_SINGERPROVIDERS_DIFFSINGER_DIFFSINGERPIPELINEEXECUTIVE_H

#include <string_view>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds {

    class DiffSingerPipelineExecutive : public Api::DiffSinger::L1::PipelineExecutive {
    public:
        DiffSingerPipelineExecutive(
            srt::SingerSpec &spec,
            const Api::DiffSinger::L1::PipelineRuntimeOptions &runtimeOptions);
        ~DiffSingerPipelineExecutive();

    public:
        srt::Expected<Api::Duration::L1::Executive *>
            createDuration(const Api::Duration::L1::RuntimeOptions &options) override;

        srt::Expected<Api::Pitch::L1::Executive *>
            createPitch(const Api::Pitch::L1::RuntimeOptions &options) override;

        srt::Expected<Api::Variance::L1::Executive *>
            createVariance(const Api::Variance::L1::RuntimeOptions &options) override;

        srt::Expected<Api::Acoustic::L1::Executive *>
            createAcoustic(const Api::Acoustic::L1::RuntimeOptions &options) override;

        srt::Expected<Api::Vocoder::L1::Executive *>
            createVocoder(const Api::Vocoder::L1::RuntimeOptions &options) override;

    private:
        srt::Expected<srt::InferenceExecutive *>
            createInference(std::string_view role, const srt::InferenceRuntimeOptions &options,
                            const srt::ContribContract &expected);
    };

}

#endif // DSINFER_SINGERPROVIDERS_DIFFSINGER_DIFFSINGERPIPELINEEXECUTIVE_H
