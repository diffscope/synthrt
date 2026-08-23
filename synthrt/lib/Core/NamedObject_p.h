#ifndef SYNTHRT_NAMEDOBJECT_P_H
#define SYNTHRT_NAMEDOBJECT_P_H

#include "NamedObject.h"

#include <map>
#include <string>
#include <vector>

namespace srt {

    class NamedObject::Impl {
    public:
        virtual ~Impl() = default;

        std::string name;
        std::map<std::string, stdc::any, std::less<>> properties;
    };

    class ObjectPool::Impl : public NamedObject::Impl {
    public:
        std::map<std::string, std::vector<NO<NamedObject>>, std::less<>> sharedObjects;
        std::map<std::string, std::vector<UNO<NamedObject>>, std::less<>> uniqueObjects;
    };

}

#endif // SYNTHRT_NAMEDOBJECT_P_H
