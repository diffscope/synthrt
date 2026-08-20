#ifndef SYNTHRT_CONTRIBHANDLER_H
#define SYNTHRT_CONTRIBHANDLER_H

#include <memory>

#include <stdcorelib/support/staticregistry.h>

#include <synthrt/Core/ContribCategory.h>

/// \file
/// What it takes to define a new kind of contribute.
///
/// A package's desc.json lists its contributes under a property per kind, and which kinds a build
/// understands is not fixed: a library linked into the program can add one, and synthrt would
/// rather it did than grow to cover everything itself.
///
/// This header is that seam, and nothing a caller of the library needs is in it -- ContribSpec.h
/// and ContribCategory.h are what they use. A kind is one handler and, where its callers want an
/// interface of their own, a \c ContribCategory and a \c ContribSpec to go with it. Neither of
/// those has to be derived, and no private header has to be reachable, to write one.

namespace srt {

    class SynthUnit;

    class PackageData;

    /// Everything synthrt does not itself know about one kind of contribute.
    ///
    /// One per kind per \c SynthUnit, built by the unit from the registry below, and outliving
    /// every specification it parses -- so it is also where a kind keeps what it wants to cache
    /// across them.
    ///
    /// \code
    ///     class InferenceHandler : public srt::ContribHandler {
    ///     protected:
    ///         srt::ContribCategory *createCategory() override;
    ///         srt::Expected<srt::ContribSpec *> parseSpec(const std::filesystem::path &basePath,
    ///                                                     const srt::JsonValue &entry) const override;
    ///         srt::Expected<void> loadSpec(srt::ContribSpec *spec,
    ///                                      srt::ContribSpec::State state) override;
    ///     };
    ///
    ///     static srt::ContribHandlerRegistry::Add<InferenceHandler>
    ///         registrar("inference", "Inference contributes");
    /// \endcode
    class SYNTHRT_EXPORT ContribHandler {
    public:
        ContribHandler();
        virtual ~ContribHandler();

    public:
        /// The name this handler was registered under. It names this kind's list inside a
        /// manifest's \c contributes object and sits between the \c ":" and the \c "/" of a
        /// reference, so it has to be a \c segment, which the unit checks as it builds handlers.
        const std::string &name() const;

        /// What callers reach this kind through.
        ContribCategory *category() const;

        /// The unit this handler was built for.
        SynthUnit *SU() const;

    protected:
        /// Builds the category callers will hold, called once, after the handler is bound to its
        /// unit. The default builds a plain \c ContribCategory; override it to hand callers one
        /// with an interface of its own.
        virtual ContribCategory *createCategory();

        /// Reads one entry of a package manifest's contributes list into a specification.
        ///
        /// The framework has already taken the entry's \c id -- that one is the package's word for
        /// the module and it fills it in afterwards. Everything else in \a entry is this kind's to
        /// read however it likes.
        virtual Expected<ContribSpec *> parseSpec(const std::filesystem::path &basePath,
                                                  const JsonValue &entry) const = 0;

        /// Moves a specification into the given state, in the order a package is loaded:
        /// \c Initialized for each, then \c Ready for each, and the reverse on the way out.
        ///
        /// Chain to this implementation, which is what keeps the index a reference resolves
        /// through up to date.
        virtual Expected<void> loadSpec(ContribSpec *spec, ContribSpec::State state);

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY_MOVE(ContribHandler)

        friend class SynthUnit;
        friend class PackageData;
        friend class ContribCategory;
    };

    /// The registered kinds, one entry per kind.
    ///
    /// \warning A \c SynthUnit reads the list once, when it is constructed, so a registration only
    ///          counts if it happens first. A library the program links against registers before
    ///          \c main and always makes it. A plugin does not: plugins load lazily through a
    ///          \c SynthUnit, so by the time one runs its static initializers, the unit that
    ///          loaded it has long since built its handlers. Contributing a kind is something a
    ///          linked library does, not something a plugin can.
    using ContribHandlerRegistry = stdc::StaticRegistry<ContribHandler>;

}

/// Keeps a caller outside synthrt from instantiating a second, empty list of its own. Without it
/// the registry reads as if nothing had ever registered, and on Windows does not even link.
///
/// Only for that caller. Inside the library this would be an instantiation declaration carrying
/// \c dllexport, which is not what that means, and the definition lives in ContribHandler.cpp
/// anyway.
#ifndef SYNTHRT_LIBRARY
extern template class SYNTHRT_EXPORT stdc::StaticRegistry<srt::ContribHandler>;
#endif

#endif // SYNTHRT_CONTRIBHANDLER_H