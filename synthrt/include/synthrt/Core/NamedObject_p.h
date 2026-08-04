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

        NamedObject *_decl;

        std::string name;
        std::map<std::string, std::any, std::less<>> properties;
    };

    class SYNTHRT_EXPORT ObjectPool::Impl : public NamedObject::Impl {
    public:
        explicit Impl(ObjectPool *decl) : NamedObject::Impl(decl) {
        }
        virtual ~Impl();

        std::map<std::string, stdc::linked_map<const NamedObject *, NO<NamedObject>>, std::less<>>
            objects;
    };

}

#endif // SYNTHRT_NAMEDOBJECT_P_H
