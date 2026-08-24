#ifndef SYNTHRT_INFERENCECONTRIB_H
#define SYNTHRT_INFERENCECONTRIB_H

#include <filesystem>
#include <memory>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    class Inference;
    class InferenceRuntimeOptions;

    /// The immutable declaration of one inference contribution.
    class SYNTHRT_EXPORT InferenceSpec : public ContribSpec {
    public:
        ~InferenceSpec();

        /// Returns the module declaration file.
        const std::filesystem::path &declarationPath() const;

        /// Creates one execution instance using the interpreter selected during Package load.
        Expected<std::unique_ptr<Inference>>
            createInference(const ContribImportOptions &importOptions,
                            const InferenceRuntimeOptions &runtimeOptions);

    private:
        explicit InferenceSpec(const ContribCreateContext &context);

        std::filesystem::path m_declarationPath;

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
    };

}

#endif // SYNTHRT_INFERENCECONTRIB_H
