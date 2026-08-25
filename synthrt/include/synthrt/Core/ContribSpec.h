#ifndef SYNTHRT_CONTRIBSPEC_H
#define SYNTHRT_CONTRIBSPEC_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Core/ContribSpecExtension.h>
#include <synthrt/Core/ContribSpecPayload.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Support/DisplayText.h>
#include <synthrt/Support/Expected.h>
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
    class ContribExecutive;
    class ContribExecutiveFactory;
    class ContribImportBinding;
    class ContribInterpreter;
    class PackageData;
    class PackageLoader;

    class ContribSpec;

    /// Validates the prepared imports of a contribution before Package Commit.
    class ContribImportValidator {
    public:
        virtual ~ContribImportValidator() = default;

        /// Validates one contribution after all import bindings and execution factories exist.
        virtual Expected<void> validateImports(const ContribSpec &spec) const = 0;

    protected:
        ContribImportValidator() = default;

        STDC_DISABLE_COPY(ContribImportValidator)
    };

    /// One ordered import declared by a contribution.
    ///
    /// This object is a non owning view. A view returned by ContribSpec remains valid only while
    /// that ContribSpec remains alive. A view returned through ContribCreateContext remains valid
    /// only for the \c createSpec() call receiving that context.
    class SYNTHRT_EXPORT ContribImport {
    public:
        ContribImport(const ContribImport &other) = default;
        ContribImport(ContribImport &&other) noexcept = default;
        ContribImport &operator=(const ContribImport &other) = default;
        ContribImport &operator=(ContribImport &&other) noexcept = default;
        ~ContribImport() = default;

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
        ContribExecutiveFactory *executiveFactory() const;

    private:
        class Data;

        /// Constructs a view over framework-owned import data.
        explicit ContribImport(const Data &data);

        const Data *m_data;

        friend class ContribCreateContext;
        friend class ContribSpec;
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

        /// Returns the common module declaration file.
        const std::filesystem::path &declarationPath() const;

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

        /// Finds the import with a role.
        ///
        /// The returned view does not retain this ContribSpec. Returns an empty optional when the
        /// role is not declared.
        std::optional<ContribImport> findImport(std::string_view role) const;

        /// \}

        /// Returns all extensions attached during Package Load.
        stdc::array_view<ContribSpecExtension *> extensions() const;

        /// Finds an extension by its identifier.
        ContribSpecExtension *findExtension(std::string_view id) const;

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

        friend class ContribExecutive;
        friend class PackageData;
        friend class PackageLoader;
    };

}

#endif // SYNTHRT_CONTRIBSPEC_H
