#include "InferenceInterpreter.h"

#include "ContribImportBinding.h"

namespace srt {

    namespace {

        class InferenceImportBinding : public ContribImportBinding {
        public:
            InferenceImportBinding(ContribSpec &importer, const ContribImport &declaration,
                                   ContribSpec &target,
                                   std::unique_ptr<ContribImportOptions> options)
                : ContribImportBinding(importer, declaration, target, std::move(options)) {
            }

        protected:
            void activate() noexcept override {
            }

            void close() noexcept override {
            }

            Expected<void> wait() override {
                return {};
            }
        };

    }

    Expected<void> InferenceInterpreter::validateImports(const ContribSpec &) const {
        return {};
    }

    Expected<void> InferenceInterpreter::validateCompatibility(const InferenceSpec &,
                                                               const InferenceSpec &) const {
        return {};
    }

    Expected<std::unique_ptr<ContribImportBinding>> InferenceInterpreter::createImportBinding(
        ContribSpec &importer, const ContribImport &declaration, ContribSpec &target,
        std::unique_ptr<ContribImportOptions> options) const {
        return std::unique_ptr<ContribImportBinding>(
            new InferenceImportBinding(importer, declaration, target, std::move(options)));
    }

}
