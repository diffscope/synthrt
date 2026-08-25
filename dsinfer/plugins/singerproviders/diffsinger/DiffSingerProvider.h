#ifndef DSINFER_DIFFSINGERPROVIDER_H
#define DSINFER_DIFFSINGERPROVIDER_H

#include <synthrt/SVS/SingerProvider.h>

namespace ds {

    class DiffSingerProvider : public srt::SingerProvider {
    public:
        DiffSingerProvider();
        ~DiffSingerProvider();

    public:
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;

        srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override;

        srt::Expected<std::unique_ptr<srt::SingerPipelineExecInstance>>
            createPipeline(srt::SingerSpec &spec,
                           const srt::SingerPipelineRuntimeOptions &runtimeOptions) override;
    };

}

#endif // DSINFER_DIFFSINGERPROVIDER_H
