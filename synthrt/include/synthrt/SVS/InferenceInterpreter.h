#ifndef SYNTHRT_INFERENCEINTERPRETER_H
#define SYNTHRT_INFERENCEINTERPRETER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/SVS/InferenceContrib.h>

namespace srt {

    class InferenceInterpreter : public NamedObject {
    public:
        /// The highest inference API version currently supported by this interpreter.
        virtual int apiLevel() const = 0;

        /// Called when \c InferenceSpec loads.
        virtual Expected<NO<InferenceSchema>> createSchema(const InferenceSpec *spec) const = 0;

        /// Called when \c InferenceSpec loads.
        virtual Expected<NO<InferenceConfiguration>>
            createConfiguration(const InferenceSpec *spec) const = 0;

        /// Called when \c SingerSpec loads.
        virtual Expected<NO<InferenceImportOptions>>
            createImportOptions(const InferenceSpec *spec, const JsonValue &options) const = 0;

        /// Called when it's about to execute an inference.
        virtual Expected<NO<Inference>>
            createInference(const InferenceSpec *spec,
                            const NO<InferenceImportOptions> &importOptions,
                            const NO<InferenceRuntimeOptions> &runtimeOptions) = 0;
    };

}

#endif // SYNTHRT_INFERENCEINTERPRETER_H