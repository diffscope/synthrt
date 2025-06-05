#include "VocoderInterpreter.h"

#include "VocoderInference.h"

namespace ds {

    VocoderInterpreter::VocoderInterpreter() = default;

    VocoderInterpreter::~VocoderInterpreter() = default;

    int VocoderInterpreter::apiLevel() const {
        return 1;
    }

    srt::Expected<srt::NO<srt::InferenceSchema>>
        VocoderInterpreter::createSchema(const srt::InferenceSpec *spec) const {
        // TODO: 读取 spec->manifestSchema() 并返回对应的 InferenceSchema 对象
        //       spec->manifestConfiguration() 也是可以读取作为参考的
        return srt::Error{
            srt::Error::FeatureNotSupported,
            "not implemented",
        };
    }

    srt::Expected<srt::NO<srt::InferenceConfiguration>>
        VocoderInterpreter::createConfiguration(const srt::InferenceSpec *spec) const {
        // TODO: 读取 spec->manifestConfiguration() 并返回对应的 InferenceConfiguration 对象
        //       spec->manifestSchema() 也是可以读取作为参考的
        return srt::Error{
            srt::Error::FeatureNotSupported,
            "not implemented",
        };
    }

    srt::Expected<srt::NO<srt::InferenceImportOptions>>
        VocoderInterpreter::createImportOptions(const srt::InferenceSpec *spec,
                                                const srt::JsonValue &options) const {
        // TODO: 读取 options 并返回对应的 InferenceImportOptions 对象
        //       spec 中所有内容均可读取作为参考
        return srt::Error{
            srt::Error::FeatureNotSupported,
            "not implemented",
        };
    }

    srt::Expected<srt::NO<srt::Inference>> VocoderInterpreter::createInference(
        const srt::InferenceSpec *spec, const srt::NO<srt::InferenceImportOptions> &importOptions,
        const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions) {
        // TODO: importOptions 和 runtimeOptions 均可读取作为参考
        return srt::NO<VocoderInference>::create(spec);
    }

}