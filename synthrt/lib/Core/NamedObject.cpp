#include "NamedObject.h"
#include "NamedObject_p.h"

#include <algorithm>
#include <utility>

namespace srt {

    NamedObject::NamedObject() : NamedObject(*new Impl()) {
    }

    NamedObject::NamedObject(std::string name) : NamedObject() {
        _impl->name = std::move(name);
    }

    NamedObject::~NamedObject() = default;

    const std::string &NamedObject::objectName() const {
        return _impl->name;
    }

    void NamedObject::setObjectName(std::string name) {
        _impl->name = std::move(name);
    }

    const stdc::any &NamedObject::property(std::string_view name) const {
        static const stdc::any empty;
        const auto it = _impl->properties.find(name);
        return it == _impl->properties.end() ? empty : it->second;
    }

    void NamedObject::setProperty(std::string_view name, stdc::any value) {
        _impl->properties.insert_or_assign(std::string(name), std::move(value));
    }

    NamedObject::NamedObject(Impl &impl) : _impl(&impl) {
    }

    ObjectPool::ObjectPool() : ObjectPool(*new Impl()) {
    }

    ObjectPool::~ObjectPool() = default;

    void ObjectPool::addSharedObject(const NO<NamedObject> &obj) {
        if (obj) {
            addSharedObject(obj->objectName(), obj);
        }
    }

    void ObjectPool::addSharedObject(std::string_view id, const NO<NamedObject> &obj) {
        if (!obj) {
            return;
        }

        auto &objects = static_cast<Impl *>(_impl.get())->sharedObjects[std::string(id)];
        if (std::find(objects.begin(), objects.end(), obj) != objects.end()) {
            return;
        }
        objects.push_back(obj);
        sharedObjectAdded(id, obj);
    }

    void ObjectPool::removeSharedObject(const NamedObject *obj) {
        auto &sets = static_cast<Impl *>(_impl.get())->sharedObjects;
        for (auto it = sets.begin(); it != sets.end();) {
            auto &objects = it->second;
            const auto object = std::find_if(objects.begin(), objects.end(),
                                             [obj](const auto &item) { return item.get() == obj; });
            if (object != objects.end()) {
                aboutToRemoveSharedObject(it->first, *object);
                objects.erase(object);
            }
            it = objects.empty() ? sets.erase(it) : ++it;
        }
    }

    void ObjectPool::removeSharedObject(std::string_view id, const NamedObject *obj) {
        auto &sets = static_cast<Impl *>(_impl.get())->sharedObjects;
        const auto setIt = sets.find(id);
        if (setIt == sets.end()) {
            return;
        }

        auto &objects = setIt->second;
        for (auto it = objects.end(); it != objects.begin();) {
            --it;
            if (it->get() == obj) {
                aboutToRemoveSharedObject(setIt->first, *it);
                objects.erase(it);
                break;
            }
        }
        if (objects.empty()) {
            sets.erase(setIt);
        }
    }

    void ObjectPool::removeSharedObjects(std::string_view id) {
        auto &sets = static_cast<Impl *>(_impl.get())->sharedObjects;
        const auto it = sets.find(id);
        if (it == sets.end()) {
            return;
        }
        for (auto object = it->second.rbegin(); object != it->second.rend(); ++object) {
            aboutToRemoveSharedObject(it->first, *object);
        }
        sets.erase(it);
    }

    void ObjectPool::removeAllSharedObjects() {
        auto &sets = static_cast<Impl *>(_impl.get())->sharedObjects;
        while (!sets.empty()) {
            removeSharedObjects(sets.rbegin()->first);
        }
    }

    std::vector<NO<NamedObject>> ObjectPool::getSharedObjects(std::string_view id) const {
        const auto &sets = static_cast<const Impl *>(_impl.get())->sharedObjects;
        const auto it = sets.find(id);
        return it == sets.end() ? std::vector<NO<NamedObject>>() : it->second;
    }

    NO<NamedObject> ObjectPool::getFirstSharedObject(std::string_view id) const {
        const auto objects = getSharedObjects(id);
        return objects.empty() ? NO<NamedObject>() : objects.front();
    }

    std::vector<NO<NamedObject>> ObjectPool::allSharedObjects() const {
        std::vector<NO<NamedObject>> result;
        for (const auto &set : static_cast<const Impl *>(_impl.get())->sharedObjects) {
            result.insert(result.end(), set.second.begin(), set.second.end());
        }
        return result;
    }

    void ObjectPool::addUniqueObject(UNO<NamedObject> obj) {
        if (obj) {
            const auto id = obj->objectName();
            addUniqueObject(id, std::move(obj));
        }
    }

    void ObjectPool::addUniqueObject(std::string_view id, UNO<NamedObject> obj) {
        if (!obj) {
            return;
        }
        auto *raw = obj.get();
        static_cast<Impl *>(_impl.get())->uniqueObjects[std::string(id)].push_back(std::move(obj));
        uniqueObjectAdded(id, raw);
    }

    void ObjectPool::removeUniqueObject(const NamedObject *obj) {
        auto &sets = static_cast<Impl *>(_impl.get())->uniqueObjects;
        for (auto it = sets.begin(); it != sets.end();) {
            auto &objects = it->second;
            const auto object = std::find_if(objects.begin(), objects.end(),
                                             [obj](const auto &item) { return item.get() == obj; });
            if (object != objects.end()) {
                aboutToRemoveUniqueObject(it->first, object->get());
                objects.erase(object);
            }
            it = objects.empty() ? sets.erase(it) : ++it;
        }
    }

    void ObjectPool::removeUniqueObject(std::string_view id, const NamedObject *obj) {
        auto &sets = static_cast<Impl *>(_impl.get())->uniqueObjects;
        const auto setIt = sets.find(id);
        if (setIt == sets.end()) {
            return;
        }

        auto &objects = setIt->second;
        for (auto it = objects.end(); it != objects.begin();) {
            --it;
            if (it->get() == obj) {
                aboutToRemoveUniqueObject(setIt->first, it->get());
                objects.erase(it);
                break;
            }
        }
        if (objects.empty()) {
            sets.erase(setIt);
        }
    }

    void ObjectPool::removeUniqueObjects(std::string_view id) {
        auto &sets = static_cast<Impl *>(_impl.get())->uniqueObjects;
        const auto it = sets.find(id);
        if (it == sets.end()) {
            return;
        }
        for (auto object = it->second.rbegin(); object != it->second.rend(); ++object) {
            aboutToRemoveUniqueObject(it->first, object->get());
        }
        sets.erase(it);
    }

    void ObjectPool::removeAllUniqueObjects() {
        auto &sets = static_cast<Impl *>(_impl.get())->uniqueObjects;
        while (!sets.empty()) {
            removeUniqueObjects(sets.rbegin()->first);
        }
    }

    std::vector<NamedObject *> ObjectPool::getUniqueObjects(std::string_view id) const {
        std::vector<NamedObject *> result;
        const auto &sets = static_cast<const Impl *>(_impl.get())->uniqueObjects;
        const auto it = sets.find(id);
        if (it == sets.end()) {
            return result;
        }
        result.reserve(it->second.size());
        for (const auto &object : it->second) {
            result.push_back(object.get());
        }
        return result;
    }

    NamedObject *ObjectPool::getFirstUniqueObject(std::string_view id) const {
        const auto objects = getUniqueObjects(id);
        return objects.empty() ? nullptr : objects.front();
    }

    std::vector<NamedObject *> ObjectPool::allUniqueObjects() const {
        std::vector<NamedObject *> result;
        for (const auto &set : static_cast<const Impl *>(_impl.get())->uniqueObjects) {
            for (const auto &object : set.second) {
                result.push_back(object.get());
            }
        }
        return result;
    }

    void ObjectPool::sharedObjectAdded(std::string_view, const NO<NamedObject> &) {
    }

    void ObjectPool::aboutToRemoveSharedObject(std::string_view, const NO<NamedObject> &) {
    }

    void ObjectPool::uniqueObjectAdded(std::string_view, NamedObject *) {
    }

    void ObjectPool::aboutToRemoveUniqueObject(std::string_view, NamedObject *) {
    }

    ObjectPool::ObjectPool(Impl &impl) : NamedObject(impl) {
    }

}
