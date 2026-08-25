#ifndef SYNTHRT_CONTRIBCATEGORY_H
#define SYNTHRT_CONTRIBCATEGORY_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <stdcorelib/adt/array_view.h>
#include <stdcorelib/support/staticregistry.h>

#include <synthrt/Core/ContribExecutive.h>
#include <synthrt/Core/ContribImportBinding.h>
#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/DisplayText.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

namespace srt {

    class PackageData;
    class PackageLoader;
    class SynthUnit;

    /// A non owning view of the data used to create one ContribSpec.
    ///
    /// The framework owns the referenced data. This view is valid only for the duration of the
    /// ContribCategory::createSpec() call that receives it.
    class SYNTHRT_EXPORT ContribCreateContext {
    public:
        const ContribLocator &locator() const;

        /// Returns the expanded contribution entry from \c desc.json.
        const JsonObject &manifestEntry() const;

        /// Returns the resolved common module declaration path.
        ///
        /// Returns \c nullptr when the category uses \c ContribCategory::EntryOnly.
        const std::filesystem::path *declarationPath() const;

        /// Returns the expanded common module declaration.
        ///
        /// Returns \c nullptr when the category uses \c ContribCategory::EntryOnly.
        const JsonObject *manifestDeclaration() const;

        /// \name Common module declaration
        ///
        /// These functions are meaningful only when the contribution category uses
        /// \c ContribCategory::ModuleDeclaration. They must not be called when it uses
        /// \c ContribCategory::EntryOnly.
        /// \{

        const DisplayText &name() const;
        const std::string &interface() const;
        const std::string &variant() const;
        int level() const;
        const JsonValue &manifestExports() const;
        const JsonValue &manifestConfiguration() const;
        stdc::array_view<ContribImport> imports() const;

        /// \}

    private:
        class Data;

        explicit ContribCreateContext(const Data &data);

        const Data *m_data;

        STDC_DISABLE_COPY(ContribCreateContext)

        friend class ContribSpec;
        friend class PackageData;
        friend class PackageLoader;
        friend class SynthUnit;
    };

    /// The index and interpreter discovery context for one contribution category.
    class SYNTHRT_EXPORT ContribCategory {
    public:
        /// Describes which declaration structure the framework parses for this category.
        enum DeclarationMode {
            /// The framework parses only the contribution entry in \c desc.json.
            EntryOnly,

            /// The framework requires \c path and parses a common module declaration file.
            ModuleDeclaration,
        };

        virtual ~ContribCategory();

        const std::string &name() const;

        DeclarationMode declarationMode() const noexcept;

        /// Returns the plugin IID used to discover interpreters for this category.
        ///
        /// This function is meaningful only when \c declarationMode() returns
        /// \c ModuleDeclaration. It must not be called for an \c EntryOnly category.
        const std::string &interpreterIid() const;

        /// Returns the SynthUnit to which this category is registered.
        ///
        /// This function may only be called after successful registration.
        SynthUnit &synthUnit() const;

        /// Casts this category to the concrete type registered for its name.
        ///
        /// The caller must first establish that \c name() identifies \c T.
        SYNTHRT_DECLARE_AS_METHODS(ContribCategory)

        /// Returns all committed contributions in this category.
        ///
        /// Each pointer remains valid only while its containing Package remains loaded.
        std::vector<ContribSpec *> contributions() const;

    protected:
        /// Creates the category specific declaration for one contribution.
        ///
        /// The framework has already parsed the common contribution envelope in \a context.
        /// Implementations parse only fields owned by their category and return the corresponding
        /// ContribSpec derived type.
        virtual Expected<std::unique_ptr<ContribSpec>>
            createSpec(const ContribCreateContext &context) const = 0;

        /// Creates the execution factory attached to an import targeting this category.
        ///
        /// Returning a null factory means that contributions in this category have no execution
        /// executive. The Loader stores a nonnull factory on the corresponding import.
        virtual Expected<std::unique_ptr<ContribExecutiveFactory>>
            createExecutiveFactory(ContribImportBinding &binding) const;

        /// Constructs a contribution category implemented by the derived class.
        ///
        /// An \c EntryOnly category is parsed entirely by \c createSpec() and does not discover or
        /// load an interpreter. Its \a interpreterIid must be empty. A \c ModuleDeclaration
        /// category uses \a interpreterIid to discover the interpreter for its declarations.
        ContribCategory(std::string name, DeclarationMode declarationMode,
                        std::string interpreterIid = {});

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class PackageData;
        friend class PackageLoader;
        friend class SynthUnit;
    };

    /// The process wide registry of contribution categories available at link time.
    ///
    /// Each SynthUnit constructs a fresh instance of every registered category. Registration must
    /// therefore finish before the SynthUnit is constructed. Loading a library afterward does not
    /// add its categories to existing units.
    using ContribCategoryRegistry = stdc::StaticRegistry<ContribCategory>;

}

#if !defined(SYNTHRT_LIBRARY) && defined(_MSC_VER)
// MSVC needs the dllimport declaration when inline registry code references the exported registry
// storage. GNU family compilers must instantiate nested registration types locally.
extern template class SYNTHRT_EXPORT stdc::StaticRegistry<srt::ContribCategory>;
#endif

#endif // SYNTHRT_CONTRIBCATEGORY_H
