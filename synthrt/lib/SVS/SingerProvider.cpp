#include "SingerProvider.h"

#include "ContribImportBinding.h"

namespace srt {

    namespace {

        class SingerExports : public ContribExports {
        public:
            explicit SingerExports(const ContribSpec &spec)
                : ContribExports(spec.interface(), spec.variant(), spec.level()) {
            }
        };

        class SingerImportOptions : public ContribImportOptions {
        public:
            explicit SingerImportOptions(const ContribSpec &target)
                : ContribImportOptions(target.interface(), target.variant(), target.level()) {
            }
        };

        class SingerImportBinding : public ContribImportBinding {
        public:
            SingerImportBinding(ContribSpec &importer, const ContribSpec::Import &declaration,
                                ContribSpec &target, std::unique_ptr<ContribImportOptions> options)
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

    Expected<std::unique_ptr<ContribExports>>
        SingerProvider::createExports(const ContribSpec &spec) const {
        return std::unique_ptr<ContribExports>(new SingerExports(spec));
    }

    Expected<std::unique_ptr<ContribImportOptions>>
        SingerProvider::createImportOptions(const ContribSpec &target,
                                            const JsonValue &manifestOptions) const {
        if (!manifestOptions.isObject()) {
            return Error(Error::InvalidFormat, "singer import options must be an object");
        }
        return std::unique_ptr<ContribImportOptions>(new SingerImportOptions(target));
    }

    Expected<void> SingerProvider::validateImports(const ContribSpec &) const {
        return {};
    }

    Expected<std::unique_ptr<ContribImportBinding>> SingerProvider::createImportBinding(
        ContribSpec &importer, const ContribSpec::Import &declaration, ContribSpec &target,
        std::unique_ptr<ContribImportOptions> options) const {
        return std::unique_ptr<ContribImportBinding>(
            new SingerImportBinding(importer, declaration, target, std::move(options)));
    }

}
