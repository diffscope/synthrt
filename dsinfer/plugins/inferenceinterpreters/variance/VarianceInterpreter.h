#ifndef DSINFER_VARIANCEINTERPRETER_H
#define DSINFER_VARIANCEINTERPRETER_H

#include <synthrt/SVS/InferenceInterpreter.h>

namespace ds {

    class VarianceInterpreter : public srt::InferenceInterpreter {
    public:
        VarianceInterpreter();
        ~VarianceInterpreter();

    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &options) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecInstance>>
            createInference(srt::InferenceSpec &spec,
                            const srt::ContribImportOptions &importOptions,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;
    };

}

#endif // DSINFER_VARIANCEINTERPRETER_H
