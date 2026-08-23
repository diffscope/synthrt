#ifndef SYNTHRT_INFERENCEINTERPRETER_H
#define SYNTHRT_INFERENCEINTERPRETER_H

#include <memory>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    class Inference;
    class InferenceSpec;

    /// Runtime options supplied when an inference instance is created.
    class InferenceRuntimeOptions : public NamedObject {
    public:
        using NamedObject::NamedObject;
        ~InferenceRuntimeOptions() override = default;
    };

    /// Interprets and executes inference contributions.
    class InferenceInterpreter : public ContribInterpreter {
    public:
        ~InferenceInterpreter() override = default;

        virtual Expected<std::unique_ptr<Inference>>
            createInference(InferenceSpec &spec, const ContribImportOptions &importOptions,
                            const InferenceRuntimeOptions &runtimeOptions) = 0;

    protected:
        InferenceInterpreter() = default;
    };

}

#endif // SYNTHRT_INFERENCEINTERPRETER_H
