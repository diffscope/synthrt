#ifndef SYNTHRT_CONTRIBCATEGORY_H
#define SYNTHRT_CONTRIBCATEGORY_H

#include <memory>
#include <string>
#include <vector>

#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    class ContribCreateContext;
    class PackageData;
    class PackageLoader;
    class SynthUnit;

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

        ContribCategory(std::string name, DeclarationMode declarationMode,
                        std::string interpreterIid = {});

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class PackageData;
        friend class PackageLoader;
        friend class SynthUnit;
    };

}

#endif // SYNTHRT_CONTRIBCATEGORY_H
