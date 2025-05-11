#include "AcousticInterpreter.h"

#include "AcousticInference.h"

namespace ds {

    AcousticInterpreter::AcousticInterpreter() {
    }

    AcousticInterpreter::~AcousticInterpreter() = default;

    int AcousticInterpreter::apiLevel() const {
        return 1;
    }

    srt::NO<srt::InferenceSchema> AcousticInterpreter::createSchema(const srt::InferenceSpec *spec,
                                                                    srt::Error *error) const {
        return {};
    }

    srt::NO<srt::InferenceConfiguration>
        AcousticInterpreter::createConfiguration(const srt::InferenceSpec *spec,
                                                 srt::Error *error) const {
        return {};
    }

    srt::NO<srt::InferenceImportOptions> AcousticInterpreter::createImportOptions(
        const srt::InferenceSpec *spec, const srt::JsonValue &options, srt::Error *error) const {
        return {};
    }

    srt::NO<srt::Inference> AcousticInterpreter::createInference(
        const srt::InferenceSpec *spec, const srt::NO<srt::InferenceImportOptions> &importOptions,
        const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions, srt::Error *error) {
        // TODO
        return std::make_shared<AcousticInference>(spec);
    }

}