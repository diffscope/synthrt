#include "NamedObject.h"
#include "NamedObject_p.h"

#include <stdcorelib/pimpl.h>

namespace srt {

    NamedObject::NamedObject() : NamedObject(*new Impl(this)) {
    }

    NamedObject::NamedObject(const std::string &name) : NamedObject() {
        setObjectName(name);
    }

    NamedObject::~NamedObject() = default;

    const std::string &NamedObject::objectName() const {
        __stdc_impl_t;
        return impl.name;
    }

    void NamedObject::setObjectName(const std::string &name) {
        __stdc_impl_t;
        impl.name = name;
    }

    NamedObject::NamedObject(Impl &impl) : _impl(&impl) {
    }

    static std::any &staticEmptyObjectProperty() {
        static std::any empty;
        return empty;
    }

    const std::any &NamedObject::property(std::string_view name) const {
        __stdc_impl_t;
        auto it = impl.properties.find(name);
        if (it == impl.properties.end()) {
            return staticEmptyObjectProperty();
        }
        return it->second;
    }

    void NamedObject::setProperty(std::string_view name, const std::any &value) {
        __stdc_impl_t;
        impl.properties[std::string(name)] = value;
    }

    ObjectPool::Impl::~Impl() {
        if (autoDelete) {
            for (const auto &item : std::as_const(objects)) {
                for (const auto &item2 : item.second) {
                    delete item2.first;
                }
            }
        }
    }

    ObjectPool::ObjectPool() : ObjectPool(*new Impl(this)) {
    }

    ObjectPool::~ObjectPool() = default;

    void ObjectPool::addObject(const NO<NamedObject> &obj) {
        addObject({}, obj);
    }

    void ObjectPool::addObject(std::string_view id, const NO<NamedObject> &obj) {
        __stdc_impl_t;

        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            it =
                impl.objects.emplace(std::string(id), stdc::linked_map<NamedObject *, int>()).first;
        }
        it->second.append(obj.get(), obj);
    }

    void ObjectPool::removeObject(const NamedObject *obj) {
        removeObject({}, obj);
    }

    void ObjectPool::removeObject(std::string_view id, const NamedObject *obj) {
        __stdc_impl_t;
        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            return;
        }
        auto &map = it->second;
        {
            auto it2 = map.find(obj);
            if (it2 == map.end()) {
                return;
            }
            aboutToRemoveObject(id, it2->second);
            map.erase(it2);
            if (map.empty()) {
                impl.objects.erase(it);
            }
        }
    }

    void ObjectPool::removeObjects(std::string_view id) {
        __stdc_impl_t;
        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            return;
        }
        auto &map = it->second;
        for (auto it = map.rbegin(); it != map.rend(); ++it) {
            aboutToRemoveObject(id, it->second);
        }
        impl.objects.erase(it);
    }

    void ObjectPool::removeAllObjects() {
        __stdc_impl_t;
        for (auto &item : impl.objects) {
            auto &map = item.second;
            for (auto it = map.rbegin(); it != map.rend(); ++it) {
                aboutToRemoveObject(item.first, it->second);
            }
        }
        impl.objects.clear();
    }

    std::vector<NO<NamedObject>> ObjectPool::allObjects() const {
        __stdc_impl_t;
        std::vector<NO<NamedObject>> res;
        for (const auto &item : impl.objects) {
            auto keys = item.second.keys();
            res.insert(res.end(), keys.begin(), keys.end());
        }
        return res;
    }

    std::vector<NO<NamedObject>> ObjectPool::getObjects(std::string_view id) const {
        __stdc_impl_t;
        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            return {};
        }
        return it->second.values();
    }

    NO<NamedObject> ObjectPool::getFirstObject(std::string_view id) const {
        __stdc_impl_t;
        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            return {};
        }
        return it->second.begin()->second;
    }

    void ObjectPool::objectAdded(std::string_view id, const NO<NamedObject> &obj) {
        (void) id;
        (void) obj;
    }

    void ObjectPool::aboutToRemoveObject(std::string_view id, const NO<NamedObject> &obj) {
        (void) id;
        (void) obj;
    }

    ObjectPool::ObjectPool(Impl &impl) : NamedObject(impl) {
    }

}