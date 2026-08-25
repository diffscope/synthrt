#ifndef SYNTHRT_INFERENCECONTRIB_H
#define SYNTHRT_INFERENCECONTRIB_H

#include <memory>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/SVS/InferenceExecInstance.h>

namespace srt {

    /// The immutable declaration of one inference contribution.
    class SYNTHRT_EXPORT InferenceSpec : public ContribSpec {
    public:
        ~InferenceSpec();

        /// Validates whether this inference can consume output from \a other.
        ///
        /// Compatibility is directional. The interpreter of this inference defines the check.
        Expected<void> validateCompatibilityWith(const InferenceSpec &other) const;

        /// Creates one execution instance using the interpreter selected during Package load.
        Expected<std::unique_ptr<InferenceExecInstance>>
            createInference(const ContribImportOptions &importOptions,
                            const InferenceRuntimeOptions &runtimeOptions);

    private:
        explicit InferenceSpec(const ContribCreateContext &context);

        friend class InferenceCategory;
    };

    /// Parses and indexes contributions in the built in \c inference category.
    class SYNTHRT_EXPORT InferenceCategory : public ContribCategory {
    public:
        InferenceCategory();
        ~InferenceCategory();

        std::vector<InferenceSpec *> inferences() const;

    protected:
        Expected<std::unique_ptr<ContribSpec>>
            createSpec(const ContribCreateContext &context) const override;

        Expected<std::unique_ptr<ContribExecFactory>>
            createExecFactory(ContribImportBinding &binding) const override;
    };

}

#endif // SYNTHRT_INFERENCECONTRIB_H
