#ifndef SYNTHRT_INFERENCEINTERPRETER_H
#define SYNTHRT_INFERENCEINTERPRETER_H

#include <memory>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Core/ContribSpecPayload.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecInstance.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    /// Interprets and executes inference contributions.
    class SYNTHRT_EXPORT InferenceInterpreter : public ContribInterpreter {
    public:
        ~InferenceInterpreter() = default;

        Expected<void> validateImports(const ContribSpec &spec) const override;

        Expected<std::unique_ptr<ContribImportBinding>>
            createImportBinding(ContribSpec &importer, const ContribImport &declaration,
                                ContribSpec &target,
                                std::unique_ptr<ContribImportOptions> options) const override;

        /// Validates whether \a spec can consume output from \a other.
        ///
        /// The default implementation accepts every other inference. Interpreters override this
        /// function only when their contract imposes restrictions between inference contracts.
        virtual Expected<void> validateCompatibility(const InferenceSpec &spec,
                                                     const InferenceSpec &other) const;

        virtual Expected<std::unique_ptr<InferenceExecInstance>>
            createInference(InferenceSpec &spec, const ContribImportOptions &importOptions,
                            const InferenceRuntimeOptions &runtimeOptions) = 0;

    protected:
        InferenceInterpreter() = default;
    };

}

#endif // SYNTHRT_INFERENCEINTERPRETER_H
