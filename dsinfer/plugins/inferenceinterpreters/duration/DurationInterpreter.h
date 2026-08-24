#ifndef DSINFER_DURATIONINTERPRETER_H
#define DSINFER_DURATIONINTERPRETER_H

#include <synthrt/SVS/InferenceInterpreter.h>

namespace ds {

    class DurationInterpreter : public srt::InferenceInterpreter {
    public:
        DurationInterpreter();
        ~DurationInterpreter();

    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &options) const override;
        srt::Expected<std::unique_ptr<srt::Inference>>
            createInference(srt::InferenceSpec &spec,
                            const srt::ContribImportOptions &importOptions,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;
    };

}

#endif // DSINFER_DURATIONINTERPRETER_H
