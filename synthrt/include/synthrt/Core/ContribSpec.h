#ifndef SYNTHRT_CONTRIBSPEC_H
#define SYNTHRT_CONTRIBSPEC_H

#include <memory>
#include <string>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/ContribReference.h>
#include <synthrt/Core/ContribSpecSubObjects.h>
#include <synthrt/Support/DisplayText.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    class ContribCategory;
    class ContribCreateContext;
    class PackageData;
    class PackageHandle;
    class PackageLoader;
    class SynthUnit;

    /// The immutable declaration of one contribution in a Package.
    class SYNTHRT_EXPORT ContribSpec {
    public:
        /// One ordered import declared by this contribution.
        class SYNTHRT_EXPORT Import {
        public:
            Import(Import &&other) noexcept;
            Import &operator=(Import &&other) noexcept;
            ~Import();

            const ContribReference &reference() const;

            /// Returns the options value read from the manifest.
            const JsonValue &manifestOptions() const;

            /// Returns options interpreted by the target interpreter during load.
            ///
            /// Returns \c nullptr for a declaration produced in \c DataOnly mode.
            const ContribImportOptions *options() const;

        private:
            Import(ContribReference reference, JsonValue options);

            class Impl;
            std::unique_ptr<Impl> _impl;

            Import(const Import &) = delete;
            Import &operator=(const Import &) = delete;

            friend class PackageData;
            friend class PackageLoader;
            friend class ContribSpec;
        };

        virtual ~ContribSpec();

        /// Returns the identity assigned by the containing Package.
        const ContribReference &reference() const;

        /// Returns a handle retaining the containing Package.
        PackageHandle package() const;

        /// \name Common module declaration
        ///
        /// These functions are meaningful only when the contribution category uses
        /// \c ContribCategory::ModuleDeclaration. They must not be called for a contribution whose
        /// category uses \c ContribCategory::EntryOnly.
        /// \{

        /// Returns the expanded common module declaration, including unknown fields.
        const JsonObject &manifestDeclaration() const;

        /// Returns the contribution name for display purposes.
        const DisplayText &name() const;

        /// Returns the contract identifier serialized as \c interface.
        const std::string &interface() const;

        /// Returns the implementation variant within the interface contract.
        const std::string &variant() const;

        /// Returns the API Level of the interface contract.
        int level() const;

        /// Returns the exports value read from the manifest.
        const JsonValue &manifestExports() const;

        /// Returns exports interpreted by the selected interpreter during load.
        ///
        /// Returns \c nullptr for a declaration produced in \c DataOnly mode.
        const ContribExports *exports() const;

        /// Returns the configuration value read from the manifest.
        const JsonValue &manifestConfiguration() const;

        /// Returns configuration interpreted by the selected interpreter during load.
        ///
        /// Returns \c nullptr for a declaration produced in \c DataOnly mode.
        const ContribConfiguration *configuration() const;

        /// Returns imports in declaration order, including repeated references.
        stdc::array_view<Import> imports() const;

        /// \}

        /// Casts this declaration to the concrete type registered for its category.
        ///
        /// The caller must first establish that \c reference().category() identifies \c T.
        SYNTHRT_DECLARE_AS_METHODS(ContribSpec)

    protected:
        /// Initializes the common declaration fields already parsed by the framework.
        explicit ContribSpec(const ContribCreateContext &context);

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class ContribCategory;
        friend class PackageData;
        friend class PackageLoader;
        friend class SynthUnit;
    };

}

#endif // SYNTHRT_CONTRIBSPEC_H
