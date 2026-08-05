#ifndef SYNTHRT_NAMEDOBJECT_P_H
#define SYNTHRT_NAMEDOBJECT_P_H

#include <map>

#include <stdcorelib/adt/linked_map.h>

#include <synthrt/Core/NamedObject.h>

/// \file
/// The implementation classes behind \c NamedObject and \c ObjectPool.
///
/// Public in the sense that a module outside synthrt can include it, which it has to in order to
/// derive from anything whose implementation derives from these. Not public in the sense of a
/// stable interface: everything here may change between any two versions.

namespace srt {

    class SYNTHRT_EXPORT NamedObject::Impl {
    public:
        explicit Impl(NamedObject *decl) : _decl(decl) {
        }
        virtual ~Impl() = default;

        /// An implementation belongs to the one object that made it, so there is nothing for a
        /// copy to mean.
        ///
        /// \note Saying so rather than leaving it implicit, because these classes are exported, and
        ///       \c dllexport instantiates every member a class has. An implicit copy constructor
        ///       is one of them, and it is compiled whether or not anyone calls it, taking every
        ///       member down with it.
        STDCORELIB_DISABLE_COPY(Impl)

        NamedObject *_decl;

        std::string name;
        std::map<std::string, stdc::any, std::less<>> properties;
    };

    class SYNTHRT_EXPORT ObjectPool::Impl : public NamedObject::Impl {
    public:
        explicit Impl(ObjectPool *decl) : NamedObject::Impl(decl) {
        }
        virtual ~Impl();

        STDCORELIB_DISABLE_COPY(Impl)

        /// The objects under one identifier, in the order they were registered, so tearing them
        /// down in reverse takes the later ones out first.
        ///
        /// \note A vector rather than \c stdc::linked_map, which holds nothing move-only: its copy
        ///       constructor copies each entry, and it is instantiated as soon as the container
        ///       becomes the mapped type of a \c std::map. A pool holds a handful of objects under
        ///       any one identifier anyway, which is what \c getFirstSharedObject assumes.
        template <class Ptr>
        using ObjectMap = std::map<std::string, std::vector<Ptr>, std::less<>>;

        ObjectMap<NO<NamedObject>> sharedObjects;
        ObjectMap<UNO<NamedObject>> uniqueObjects;
    };

}

#endif // SYNTHRT_NAMEDOBJECT_P_H
