#ifndef DSINFER_VARIANCEINTERPRETER_H
#define DSINFER_VARIANCEINTERPRETER_H

#include <synthrt/SVS/InferenceInterpreter.h>

namespace srt::svs {

    class VarianceInterpreter : public srt::svs::InferenceInterpreter {
    public:
        VarianceInterpreter();
        ~VarianceInterpreter();

    public:
        int apiLevel() const override;
        srt::core::Expected<srt::core::NO<srt::svs::InferenceSchema>>
            createSchema(const srt::svs::InferenceSpec *spec) const override;
        srt::core::Expected<srt::core::NO<srt::svs::InferenceConfiguration>>
            createConfiguration(const srt::svs::InferenceSpec *spec) const override;
        srt::core::Expected<srt::core::NO<srt::svs::InferenceImportOptions>>
            createImportOptions(const srt::svs::InferenceSpec *spec,
                                const srt::core::JsonValue &options) const override;
        srt::core::Expected<srt::core::NO<srt::svs::Inference>>
            createInference(const srt::svs::InferenceSpec *spec,
                            const srt::core::NO<srt::svs::InferenceImportOptions> &importOptions,
                            const srt::core::NO<srt::svs::InferenceRuntimeOptions> &runtimeOptions) override;
    };

}

#endif // DSINFER_VARIANCEINTERPRETER_H