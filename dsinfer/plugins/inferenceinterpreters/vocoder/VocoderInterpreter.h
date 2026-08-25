#ifndef DSINFER_VOCODERINTERPRETER_H
#define DSINFER_VOCODERINTERPRETER_H

#include <synthrt/SVS/InferenceInterpreter.h>

namespace ds {

    class VocoderInterpreter : public srt::InferenceInterpreter {
    public:
        VocoderInterpreter();
        ~VocoderInterpreter();

    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &options) const override;
        srt::Expected<void> validateCompatibility(const srt::InferenceSpec &spec,
                                                  const srt::InferenceSpec &other) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecInstance>>
            createInference(srt::InferenceSpec &spec,
                            const srt::ContribImportOptions &importOptions,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;
    };

}

#endif // DSINFER_VOCODERINTERPRETER_H
