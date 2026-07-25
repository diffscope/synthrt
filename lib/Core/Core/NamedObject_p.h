#pragma once

#include <map>

#include <stdcorelib/adt/linked_map.h>

#include <synthrt/Core/Core/NamedObject.h>

namespace srt::core {

    class NamedObject::Impl {
    public:
        explicit Impl(NamedObject *q) : m_q(q) {
        }
        virtual ~Impl() = default;

        NamedObject *m_q;

        std::string m_name;
        std::map<std::string, std::any, std::less<>> m_properties;
    };

    class ObjectPool::Impl : public NamedObject::Impl {
    public:
        explicit Impl(ObjectPool *q) : NamedObject::Impl(q) {
        }
        virtual ~Impl();

        std::map<std::string, stdc::linked_map<const NamedObject *, NO<NamedObject>>, std::less<>>
            m_objects;
    };

}
