#ifndef SYNTHRT_INFERENCEINTERPRETER_H
#define SYNTHRT_INFERENCEINTERPRETER_H

#include <synthrt/SVS/InferenceContrib.h>

namespace srt {

    class InferenceInterpreter {
    public:
        virtual ~InferenceInterpreter() = default;

    public:
        /// The highest inference API version currently supported by this interpreter.
        virtual int apiLevel() const = 0;

        /// Called when \a InferenceSpec loads.
        virtual NO<InferenceSchema> createSchema(const InferenceSpec *spec, Error *error) const = 0;

        /// Called when \a InferenceSpec loads.
        virtual NO<InferenceConfiguration> createConfiguration(const InferenceSpec *spec,
                                                               Error *error) const = 0;

        // Called when \a SingerSpec loads.
        virtual NO<InferenceImportOptions> createImportOptions(const InferenceSpec *spec,
                                                               const JsonValue &options,
                                                               Error *error) const = 0;

        // Called when it's about to execute an inference.
        virtual Inference *createInference(const InferenceSpec *spec,
                                           const NO<InferenceImportOptions> &importOptions,
                                           const NO<InferenceRuntimeOptions> &runtimeOptions,
                                           Error *error) = 0;

    public:
        STDCORELIB_DISABLE_COPY(InferenceInterpreter)
    };

}

#endif // SYNTHRT_INFERENCEINTERPRETER_H