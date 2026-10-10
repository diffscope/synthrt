#ifndef DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICINTERPRETER_H
#define DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICINTERPRETER_H

#include <synthrt/SVS/InferenceInterpreter.h>

namespace ds {

    class AcousticInterpreter : public srt::InferenceInterpreter {
    public:
        AcousticInterpreter();
        ~AcousticInterpreter();

    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &options) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec,
                            const srt::ContribImportOptions &importOptions,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;
    };

}

#endif // DSINFER_INFERENCEINTERPRETERS_ACOUSTIC_ACOUSTICINTERPRETER_H
