#ifndef DSINFER_VOCODERINTERPRETER_H
#define DSINFER_VOCODERINTERPRETER_H

#include <synthrt/SVS/InferenceInterpreter.h>

namespace ds {

    class VocoderInterpreter : public srt::InferenceInterpreter {
    public:
        VocoderInterpreter();
        ~VocoderInterpreter();

    public:
        int apiLevel() const override;
        srt::NO<srt::InferenceSchema> createSchema(const srt::InferenceSpec *spec,
                                                   srt::Error *error) const override;
        srt::NO<srt::InferenceConfiguration> createConfiguration(const srt::InferenceSpec *spec,
                                                                 srt::Error *error) const override;
        srt::NO<srt::InferenceImportOptions> createImportOptions(const srt::InferenceSpec *spec,
                                                                 const srt::JsonValue &options,
                                                                 srt::Error *error) const override;
        srt::NO<srt::Inference>
            createInference(const srt::InferenceSpec *spec,
                            const srt::NO<srt::InferenceImportOptions> &importOptions,
                            const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions,
                            srt::Error *error) override;
    };

}

#endif // DSINFER_VOCODERINTERPRETER_H