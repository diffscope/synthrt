#ifndef SYNTHRT_CONTRIBHANDLER_H
#define SYNTHRT_CONTRIBHANDLER_H

#include <memory>
#include <type_traits>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Contribute.h>

/// \file
/// What the author of a contribute kind writes, as opposed to what a caller of the library uses.
///
/// A contribute kind is two objects, not one. \c ContribSpec and \c ContribCategory are the faces
/// the rest of the program holds and calls; the handlers below are where whoever defines the kind
/// puts the state and the behaviour only they know about. The face owns its handler and hands it
/// out to nobody.
///
/// The split is what keeps \c Contribute.h free of the machinery an extension needs, and it lets
/// the two halves change independently: a handler can grow a virtual without touching the vtable
/// of a class every caller of the library has compiled against.
///
/// \sa ContribCategoryRegistry, which is how a kind announces itself.

namespace srt {

    /// The author's half of one contribute.
    ///
    /// Derive it, put the fields read out of the module's manifest in it, and hand it to
    /// \c ContribSpec 's constructor. Nothing here is called by the framework -- reading the
    /// manifest is arranged by the category, which knows the concrete type -- so the shape of that
    /// reading is the author's to choose.
    class SYNTHRT_EXPORT ContribSpecHandler {
    public:
        ContribSpecHandler();
        virtual ~ContribSpecHandler();

    public:
        /// The manifest format version the module declared, which every kind so far spells
        /// \c $version.
        stdc::VersionNumber fmtVersion;

        STDC_DISABLE_COPY(ContribSpecHandler)
    };

    namespace detail {

        /// Casts to the handler type while carrying constness across, so that a const member
        /// function sees a const handler. \sa srt_handler_t
        template <class T, class H>
        inline auto contrib_handler_cast(H *handler) {
            return static_cast<std::conditional_t<std::is_const_v<H>, const T, T> *>(handler);
        }

    }

}

/// Declares a \c handler reference to the current object's nested \c Handler, the way
/// \c stdc_impl_t declares \c impl.
///
/// \code
///     DisplayText InferenceSpec::name() const {
///         srt_handler_t;
///         return handler.name;
///     }
/// \endcode
#define srt_handler(T)                                                                             \
    auto &handler = *::srt::detail::contrib_handler_cast<typename T::Handler>(handlerObject())
#define srt_handler_t srt_handler(std::remove_pointer_t<decltype(this)>)

#endif // SYNTHRT_CONTRIBHANDLER_H
