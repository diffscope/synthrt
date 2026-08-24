#ifndef SYNTHRT_INFERENCEINTERPRETER_H
#define SYNTHRT_INFERENCEINTERPRETER_H

#include <memory>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Core/ContribSpecPayload.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    class InferenceSpec;

    /// Interprets and executes inference contributions.
    class SYNTHRT_EXPORT InferenceInterpreter : public ContribInterpreter {
    public:
        ~InferenceInterpreter() = default;

        Expected<void> validateImports(const ContribSpec &spec) const override;

        Expected<std::unique_ptr<ContribImportBinding>>
            createImportBinding(ContribSpec &importer, const ContribSpec::Import &declaration,
                                ContribSpec &target,
                                std::unique_ptr<ContribImportOptions> options) const override;

        virtual Expected<std::unique_ptr<Inference>>
            createInference(InferenceSpec &spec, const ContribImportOptions &importOptions,
                            const InferenceRuntimeOptions &runtimeOptions) = 0;

    protected:
        InferenceInterpreter() = default;
    };

}

#endif // SYNTHRT_INFERENCEINTERPRETER_H
