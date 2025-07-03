#ifndef DSINFER_PITCHINTERPRETER_H
#define DSINFER_PITCHINTERPRETER_H

#include <synthrt/SVS/InferenceInterpreter.h>

namespace ds {

    class PitchInterpreter : public srt::InferenceInterpreter {
    public:
        PitchInterpreter();
        ~PitchInterpreter();

    public:
        int apiLevel() const override;
        srt::Expected<srt::NO<srt::InferenceSchema>>
            createSchema(const srt::InferenceSpec *spec) const override;
        srt::Expected<srt::NO<srt::InferenceConfiguration>>
            createConfiguration(const srt::InferenceSpec *spec) const override;
        srt::Expected<srt::NO<srt::InferenceImportOptions>>
            createImportOptions(const srt::InferenceSpec *spec,
                                const srt::JsonValue &options) const override;
        srt::Expected<srt::NO<srt::Inference>>
            createInference(const srt::InferenceSpec *spec,
                            const srt::NO<srt::InferenceImportOptions> &importOptions,
                            const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions) override;
    };

}

#endif // DSINFER_PITCHINTERPRETER_H