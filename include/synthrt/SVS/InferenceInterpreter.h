#pragma once

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/Core/srt_core_global.h>
#include <synthrt/SVS/srt_svs_global.h>

namespace srt::svs {

    class SRT_SVS_EXPORT InferenceInterpreter : public core::NamedObject {
    public:
        virtual int apiLevel() const = 0;

        virtual core::Expected<core::NO<InferenceSchema>>
            createSchema(const InferenceSpec *spec) const = 0;

        virtual core::Expected<core::NO<InferenceConfiguration>>
            createConfiguration(const InferenceSpec *spec) const = 0;

        virtual core::Expected<core::NO<InferenceImportOptions>>
            createImportOptions(const InferenceSpec *spec,
                                const core::JsonValue &options) const = 0;

        virtual core::Expected<core::NO<Inference>>
            createInference(const InferenceSpec *spec,
                            const core::NO<InferenceImportOptions> &importOptions,
                            const core::NO<InferenceRuntimeOptions> &runtimeOptions) = 0;
    };

}
