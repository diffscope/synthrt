#include "AcousticInterpreter.h"

#include "AcousticInference.h"

namespace ds {

    AcousticInterpreter::AcousticInterpreter() = default;

    AcousticInterpreter::~AcousticInterpreter() = default;

    int AcousticInterpreter::apiLevel() const {
        return 1;
    }

    srt::NO<srt::InferenceSchema> AcousticInterpreter::createSchema(const srt::InferenceSpec *spec,
                                                                    srt::Error *error) const {
        // TODO: 读取 spec->manifestSchema() 并返回对应的 InferenceSchema 对象
        //       spec->manifestConfiguration() 也是可以读取作为参考的
        return {};
    }

    srt::NO<srt::InferenceConfiguration>
        AcousticInterpreter::createConfiguration(const srt::InferenceSpec *spec,
                                                 srt::Error *error) const {
        // TODO: 读取 spec->manifestConfiguration() 并返回对应的 InferenceConfiguration 对象
        //       spec->manifestSchema() 也是可以读取作为参考的
        return {};
    }

    srt::NO<srt::InferenceImportOptions> AcousticInterpreter::createImportOptions(
        const srt::InferenceSpec *spec, const srt::JsonValue &options, srt::Error *error) const {
        // TODO: 读取 options 并返回对应的 InferenceImportOptions 对象
        //       spec 中所有内容均可读取作为参考
        return {};
    }

    srt::NO<srt::Inference> AcousticInterpreter::createInference(
        const srt::InferenceSpec *spec, const srt::NO<srt::InferenceImportOptions> &importOptions,
        const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions, srt::Error *error) {
        // TODO: importOptions 和 runtimeOptions 均可读取作为参考
        return srt::NO<AcousticInference>::create(spec);
    }

}