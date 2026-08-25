#ifndef SYNTHRT_CONTRIBSPEC_H
#define SYNTHRT_CONTRIBSPEC_H

#include <memory>
#include <string>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Core/ContribSpecPayload.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Support/DisplayText.h>
#include <synthrt/Support/JSON.h>

namespace srt {

    /// A typed interpretation of one contribution's manifest \c exports value.
    class ContribExports : public ContribSpecPayload {
    public:
        virtual ~ContribExports() = default;

    protected:
        using ContribSpecPayload::ContribSpecPayload;
    };

    /// A typed interpretation of one import entry's manifest \c options value.
    class ContribImportOptions : public ContribSpecPayload {
    public:
        virtual ~ContribImportOptions() = default;

    protected:
        using ContribSpecPayload::ContribSpecPayload;
    };

    /// A typed interpretation of one contribution's manifest \c configuration value.
    class ContribConfiguration : public ContribSpecPayload {
    public:
        virtual ~ContribConfiguration() = default;

    protected:
        using ContribSpecPayload::ContribSpecPayload;
    };

    class ContribCreateContext;
    class ContribExecInstance;
    class ContribExecFactory;
    class ContribImportBinding;
    class ContribImportData;
    class ContribInterpreter;
    class PackageData;
    class PackageLoader;

    /// One ordered import declared by a contribution.
    class SYNTHRT_EXPORT ContribImport {
    public:
        ContribImport(ContribImport &&other) noexcept;
        ContribImport &operator=(ContribImport &&other) noexcept;
        ~ContribImport();

        /// Returns the role unique within the importing contribution.
        const std::string &role() const;

        const ContribLocator &locator() const;

        /// Returns the options value read from the manifest.
        const JsonValue &manifestOptions() const;

        /// Returns options interpreted by the target interpreter during load.
        ///
        /// Returns \c nullptr for a declaration produced in \c DataOnly mode.
        const ContribImportOptions *options() const;

        /// Returns the runtime binding created during load.
        ///
        /// Returns \c nullptr for a declaration produced in \c DataOnly mode.
        ContribImportBinding *binding() const;

        /// Returns the target category's execution factory for this import.
        ContribExecFactory *execFactory() const;

    private:
        ContribImport(std::string role, ContribLocator locator, JsonValue options);
        ContribImport(const ContribImport &other);

        std::shared_ptr<ContribImportData> m_data;

        ContribImport &operator=(const ContribImport &) = delete;

        friend class ContribExecInstance;
        friend class ContribSpec;
        friend class PackageData;
        friend class PackageLoader;
    };

    /// The immutable declaration of one contribution in a Package.
    class SYNTHRT_EXPORT ContribSpec {
    public:
        virtual ~ContribSpec();

        /// Returns the identity assigned by the containing Package.
        const ContribLocator &locator() const;

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

        /// Returns imports in declaration order, including repeated locators.
        stdc::array_view<ContribImport> imports() const;

        /// \}

        /// Casts this declaration to the concrete type registered for its category.
        ///
        /// The caller must first establish that \c locator().category() identifies \c T.
        SYNTHRT_DECLARE_AS_METHODS(ContribSpec)

    protected:
        /// Initializes the common declaration fields already parsed by the framework.
        explicit ContribSpec(const ContribCreateContext &context);

        /// Returns the interpreter selected while loading this contribution.
        ContribInterpreter *interpreter() const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class ContribExecInstance;
        friend class PackageData;
        friend class PackageLoader;
    };

}

#endif // SYNTHRT_CONTRIBSPEC_H
