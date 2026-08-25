#ifndef SYNTHRT_CONTRIBSPECEXTENSION_H
#define SYNTHRT_CONTRIBSPECEXTENSION_H

#include <memory>
#include <string>

#include <stdcorelib/support/staticregistry.h>

#include <synthrt/Support/Expected.h>

namespace srt {

    class ContribSpec;

    /// Maps a contribution declaration type and an execution instance type to an extension ID.
    template <class Spec, class ExecInstance>
    struct ContribSpecExtensionTraits;

    /// Adds library defined behavior to one loaded contribution declaration.
    ///
    /// Extensions are created after imports and their execution factories are prepared. The
    /// containing ContribSpec owns each extension and outlives it.
    class SYNTHRT_EXPORT ContribSpecExtension {
    public:
        virtual ~ContribSpecExtension();

        /// Returns the identifier unique within the containing contribution.
        const std::string &id() const;

        /// Returns the contribution extended by this object.
        ContribSpec &spec() const;

        /// Finds the extension associated with an execution instance type in a spec.
        template <class ExecInstance, class Spec>
        static ContribSpecExtension *findFromSpec(const Spec &spec) {
            using Traits = ContribSpecExtensionTraits<Spec, ExecInstance>;
            return spec.findExtension(Traits::ID);
        }

        SYNTHRT_DECLARE_AS_METHODS(ContribSpecExtension)

    protected:
        ContribSpecExtension(ContribSpec &spec, std::string id);

    private:
        ContribSpec *m_spec;
        std::string m_id;

        STDC_DISABLE_COPY(ContribSpecExtension)
    };

    /// Creates an extension for a matching contribution before Package Commit.
    class ContribSpecExtensionFactory {
    public:
        virtual ~ContribSpecExtensionFactory() = default;

        /// Returns whether this factory applies to a spec.
        virtual bool matches(const ContribSpec &spec) const noexcept = 0;

        /// Creates an extension for a matching spec.
        virtual Expected<std::unique_ptr<ContribSpecExtension>> create(ContribSpec &spec) const = 0;

    protected:
        ContribSpecExtensionFactory() = default;

        STDC_DISABLE_COPY(ContribSpecExtensionFactory)
    };

    /// The process wide registry of contribution extension factories available at Load time.
    using ContribSpecExtensionFactoryRegistry = stdc::StaticRegistry<ContribSpecExtensionFactory>;

}

#if !defined(SYNTHRT_LIBRARY) && defined(_MSC_VER)
extern template class SYNTHRT_EXPORT stdc::StaticRegistry<srt::ContribSpecExtensionFactory>;
#endif

#endif // SYNTHRT_CONTRIBSPECEXTENSION_H
