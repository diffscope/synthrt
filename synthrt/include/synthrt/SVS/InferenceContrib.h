#ifndef SYNTHRT_INFERENCECONTRIB_H
#define SYNTHRT_INFERENCECONTRIB_H

#include <memory>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/SVS/InferenceExecutive.h>

namespace srt {

    /// The immutable declaration of one inference contribution.
    class SYNTHRT_EXPORT InferenceSpec : public ContribSpec {
    public:
        ~InferenceSpec();

        /// Validates whether this inference can consume output from \a other.
        ///
        /// Compatibility is directional. The interpreter of this inference defines the check.
        Expected<void> validateCompatibilityWith(const InferenceSpec &other) const;

        /// Creates one root executive using the interpreter selected during Package load.
        ///
        /// \a importOptions describes how the caller imports this inference contract.
        /// \a runtimeOptions supplies the options for this execution. Both objects must have the
        /// same interface, variant, and Level as this specification.
        ///
        /// The caller exclusively owns the returned executive and must destroy it before the
        /// Package containing this specification is released. The returned executive has no
        /// parent. A derived \c ContribExecutive that creates an imported child should use
        /// \c createChild instead so the child joins its supervision tree.
        Expected<std::unique_ptr<InferenceExecutive>>
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

        Expected<std::unique_ptr<ContribExecutiveFactory>>
            createExecutiveFactory(ContribImportBinding &binding) const override;
    };

}

#endif // SYNTHRT_INFERENCECONTRIB_H
