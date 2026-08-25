#ifndef SYNTHRT_CONTRIBINTERPRETER_H
#define SYNTHRT_CONTRIBINTERPRETER_H

#include <memory>
#include <vector>

#include <synthrt/Core/ContribImportBinding.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

namespace srt {

    /// Interprets one module contract implementation.
    ///
    /// The same interpreter instance may serve multiple contributions that use the same
    /// interface, Level, and variant triple.
    class ContribInterpreter {
    public:
        virtual ~ContribInterpreter() = default;

        /// Creates the import validators made available by this interpreter.
        virtual Expected<std::vector<std::unique_ptr<ContribImportValidator>>>
            createImportValidators() const {
            return std::vector<std::unique_ptr<ContribImportValidator>>();
        }

        /// Creates every extension supplied by this interpreter for a spec.
        ///
        /// Returns an empty collection when this interpreter does not extend a spec.
        virtual Expected<std::vector<std::unique_ptr<ContribSpecExtension>>>
            createExtensions(ContribSpec &spec) const {
            return std::vector<std::unique_ptr<ContribSpecExtension>>();
        }

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

        /// Creates a prepared runtime binding owned by \a importer.
        ///
        /// The binding takes ownership of \a options and must remain closed until the Loader
        /// activates it during Commit.
        virtual Expected<std::unique_ptr<ContribImportBinding>>
            createImportBinding(ContribSpec &importer, const ContribImport &declaration,
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
