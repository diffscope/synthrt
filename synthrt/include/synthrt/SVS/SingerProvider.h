#ifndef SYNTHRT_SINGERPROVIDER_H
#define SYNTHRT_SINGERPROVIDER_H

#include <synthrt/Core/ContribInterpreter.h>

namespace srt {

    /// Interprets and executes singer contributions.
    class SYNTHRT_EXPORT SingerProvider : public ContribInterpreter {
    public:
        ~SingerProvider() = default;

        Expected<std::unique_ptr<ContribExports>>
            createExports(const ContribSpec &spec) const override;

        Expected<std::unique_ptr<ContribImportOptions>>
            createImportOptions(const ContribSpec &target,
                                const JsonValue &manifestOptions) const override;

        Expected<void> validateImports(const ContribSpec &spec) const override;

        Expected<std::unique_ptr<ContribImportBinding>>
            createImportBinding(ContribSpec &importer, const ContribSpec::Import &declaration,
                                ContribSpec &target,
                                std::unique_ptr<ContribImportOptions> options) const override;

    protected:
        SingerProvider() = default;
    };

}

#endif // SYNTHRT_SINGERPROVIDER_H
