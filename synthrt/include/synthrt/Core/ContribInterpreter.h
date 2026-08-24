#ifndef SYNTHRT_CONTRIBINTERPRETER_H
#define SYNTHRT_CONTRIBINTERPRETER_H

#include <memory>

#include <synthrt/Core/ContribImportBinding.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    /// Interprets one module contract implementation.
    ///
    /// The same interpreter instance may serve multiple contributions that use the same
    /// interface, Level, and variant triple.
    class SYNTHRT_EXPORT ContribInterpreter {
    public:
        virtual ~ContribInterpreter();

        /// Interprets the manifest exports of \a spec.
        virtual Expected<std::unique_ptr<ContribExports>>
            createExports(const ContribSpec &spec) const = 0;

        /// Interprets the manifest configuration of \a spec.
        virtual Expected<std::unique_ptr<ContribConfiguration>>
            createConfiguration(const ContribSpec &spec) const = 0;

        /// Interprets one import's manifest options using the contract of \a target.
        virtual Expected<std::unique_ptr<ContribImportOptions>>
            createImportOptions(const ContribSpec &target,
                                const JsonValue &manifestOptions) const = 0;

        /// Validates the interpreted, ordered imports of \a spec as one collection.
        virtual Expected<void> validateImports(const ContribSpec &spec) const = 0;

        /// Creates a prepared runtime binding owned by \a importer.
        ///
        /// The binding takes ownership of \a options and must remain closed until the Loader
        /// activates it during Commit.
        virtual Expected<std::unique_ptr<ContribImportBinding>>
            createImportBinding(ContribSpec &importer, const ContribSpec::Import &declaration,
                                ContribSpec &target,
                                std::unique_ptr<ContribImportOptions> options) const = 0;

        /// Casts this interpreter to a category specific interpreter type.
        SYNTHRT_DECLARE_AS_METHODS(ContribInterpreter)

    protected:
        ContribInterpreter() = default;

        STDC_DISABLE_COPY(ContribInterpreter)
    };

}

#endif // SYNTHRT_CONTRIBINTERPRETER_H
