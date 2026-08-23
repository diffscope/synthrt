#ifndef SYNTHRT_CONTRIBCREATECONTEXT_H
#define SYNTHRT_CONTRIBCREATECONTEXT_H

#include <filesystem>
#include <string>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/ContribReference.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/DisplayText.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    class ContribCategory;
    class PackageData;
    class SynthUnit;

    /// A non owning view of the data used to create one ContribSpec.
    ///
    /// The framework owns the referenced data. This view is valid only for the duration of the
    /// ContribCategory::createSpec() call that receives it.
    class SYNTHRT_EXPORT ContribCreateContext {
    public:
        const ContribReference &reference() const;

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
        stdc::array_view<ContribSpec::Import> imports() const;

        /// \}

    private:
        class Data;

        explicit ContribCreateContext(const Data &data);

        const Data *m_data;

        STDC_DISABLE_COPY(ContribCreateContext)

        friend class ContribSpec;
        friend class PackageData;
        friend class SynthUnit;
    };

}

#endif // SYNTHRT_CONTRIBCREATECONTEXT_H
