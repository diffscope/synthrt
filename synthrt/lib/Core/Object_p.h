#ifndef SYNTHRT_OBJECT_P_H
#define SYNTHRT_OBJECT_P_H

#include <map>

#include <stdcorelib/linked_map.h>

#include <synthrt/Core/Object.h>

namespace srt {

    class NamedObject::Impl {
    public:
        explicit Impl(NamedObject *decl) : _decl(decl) {
        }
        virtual ~Impl() = default;

        NamedObject *_decl;

        std::string name;
    };

    class Object::Impl : public NamedObject::Impl {
    public:
        explicit Impl(Object *decl);
        ~Impl();

        void deleteChildren();
        void setParent(Object *o);

        std::string name;
        std::map<std::string, std::any, std::less<>> properties;

        Object *parent = nullptr;
        std::vector<Object *> children;

        bool wasDeleted = false;
        Object *currentChildBeingDeleted = nullptr;
        bool isDeletingChildren = false;
    };

    class ObjectPool::Impl : public Object::Impl {
    public:
        explicit Impl(ObjectPool *decl) : Object::Impl(decl) {
        }
        virtual ~Impl();

        std::map<std::string, stdc::linked_map<Object *, int /*NOT USED*/>, std::less<>> objects;

        bool autoDelete = true;
    };

}

#endif // SYNTHRT_OBJECT_P_H