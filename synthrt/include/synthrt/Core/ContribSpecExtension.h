#ifndef SYNTHRT_CONTRIBSPECEXTENSION_H
#define SYNTHRT_CONTRIBSPECEXTENSION_H

#include <string>

#include <synthrt/synthrt_global.h>

namespace srt {

    class ContribSpec;

    /// Maps a contribution declaration type and an executive type to an extension ID.
    template <class Spec, class Executive>
    struct ContribSpecExtensionTraits;

    /// Adds library defined behavior to one loaded contribution declaration.
    ///
    /// Extensions are created in the last step of the Ready phase, after every import of the new
    /// package has its options, binding and execution factory, and after every registered import
    /// validator has accepted the package. An extension may therefore read the exports,
    /// configuration, import options and bindings of the declaration it extends and of the
    /// declarations those imports refer to, and may rely on the guarantees of the validators. The
    /// containing ContribSpec owns each extension and outlives it.
    class SYNTHRT_EXPORT ContribSpecExtension {
    public:
        virtual ~ContribSpecExtension();

        /// Returns the identifier unique within the containing contribution.
        const std::string &id() const;

        /// Returns the contribution extended by this object.
        ContribSpec &spec() const;

        /// Finds the extension associated with an executive type in a spec.
        template <class Executive, class Spec>
        static ContribSpecExtension *findFromSpec(const Spec &spec) {
            using Traits = ContribSpecExtensionTraits<Spec, Executive>;
            return spec.findExtension(Traits::ID);
        }

        SYNTHRT_DECLARE_AS_METHODS(ContribSpecExtension)

    protected:
        ContribSpecExtension(ContribSpec &spec, std::string id);

    private:
        ContribSpec *m_spec;
        std::string m_id;

        STDC_DISABLE_COPY_MOVE(ContribSpecExtension)
    };

}

#endif // SYNTHRT_CONTRIBSPECEXTENSION_H
