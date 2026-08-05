#include "NamedObject.h"
#include "NamedObject_p.h"

#include <stdcorelib/pimpl.h>

namespace srt {

    NamedObject::NamedObject() : NamedObject(*new Impl(this)) {
    }

    NamedObject::NamedObject(std::string name) : NamedObject() {
        setObjectName(std::move(name));
    }

    NamedObject::~NamedObject() = default;

    const std::string &NamedObject::objectName() const {
        stdc_impl_t;
        return impl.name;
    }

    void NamedObject::setObjectName(std::string name) {
        stdc_impl_t;
        impl.name = std::move(name);
    }

    static stdc::any &staticEmptyObjectProperty() {
        static stdc::any empty;
        return empty;
    }

    const stdc::any &NamedObject::property(std::string_view name) const {
        stdc_impl_t;
        auto it = impl.properties.find(name);
        if (it == impl.properties.end()) {
            return staticEmptyObjectProperty();
        }
        return it->second;
    }

    void NamedObject::setProperty(std::string_view name, stdc::any value) {
        stdc_impl_t;
        auto it = impl.properties.find(name);
        if (it == impl.properties.end()) {
            impl.properties[std::string(name)] = std::move(value);
        } else {
            it->second = std::move(value);
        }
    }

    NamedObject::NamedObject(Impl &impl) : _impl(&impl) {
    }

    ObjectPool::Impl::~Impl() {
    }

    ObjectPool::ObjectPool() : ObjectPool(*new Impl(this)) {
    }

    ObjectPool::~ObjectPool() = default;


    namespace {

        /// Registers \a obj under \a id, unless it is already there.
        /// \return the object once it is in, or null if it was already.
        template <class Map, class Ptr>
        NamedObject *addTo(Map &map, std::string_view id, Ptr obj) {
            auto raw = obj.get();
            auto &objects = map[std::string(id)];
            for (const auto &item : objects) {
                if (item.get() == raw) {
                    return nullptr;
                }
            }
            objects.push_back(std::move(obj));
            return raw;
        }

        /// Removes one entry, dropping the whole identifier once it holds nothing.
        template <class Map, class BeforeRemove>
        void eraseOne(Map &map, std::string_view id, const NamedObject *obj,
                      BeforeRemove &&beforeRemove) {
            auto it = map.find(id);
            if (it == map.end()) {
                return;
            }
            auto &objects = it->second;
            for (auto it2 = objects.begin(); it2 != objects.end(); ++it2) {
                if (it2->get() != obj) {
                    continue;
                }
                beforeRemove(it->first, *it2);
                objects.erase(it2);
                if (objects.empty()) {
                    map.erase(it);
                }
                return;
            }
        }

        /// Removes every entry under \a id, last first, so what was registered later is torn down
        /// before what it was registered after.
        template <class Map, class BeforeRemove>
        void eraseUnder(Map &map, std::string_view id, BeforeRemove &&beforeRemove) {
            auto it = map.find(id);
            if (it == map.end()) {
                return;
            }
            auto &objects = it->second;
            for (auto it2 = objects.rbegin(); it2 != objects.rend(); ++it2) {
                beforeRemove(it->first, *it2);
            }
            map.erase(it);
        }

        template <class Map, class BeforeRemove>
        void eraseEverything(Map &map, BeforeRemove &&beforeRemove) {
            for (auto &item : map) {
                auto &objects = item.second;
                for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
                    beforeRemove(item.first, *it);
                }
            }
            map.clear();
        }

    }

    void ObjectPool::addSharedObject(const NO<NamedObject> &obj) {
        addSharedObject({}, obj);
    }

    void ObjectPool::addSharedObject(std::string_view id, const NO<NamedObject> &obj) {
        stdc_impl_t;
        if (!obj) {
            return;
        }
        if (addTo(impl.sharedObjects, id, obj)) {
            sharedObjectAdded(id, obj);
        }
    }

    void ObjectPool::removeSharedObject(const NamedObject *obj) {
        removeSharedObject({}, obj);
    }

    void ObjectPool::removeSharedObject(std::string_view id, const NamedObject *obj) {
        stdc_impl_t;
        eraseOne(impl.sharedObjects, id, obj,
                 [this](std::string_view key, const NO<NamedObject> &o) {
                     aboutToRemoveSharedObject(key, o);
                 });
    }

    void ObjectPool::removeSharedObjects(std::string_view id) {
        stdc_impl_t;
        eraseUnder(impl.sharedObjects, id, [this](std::string_view key, const NO<NamedObject> &o) {
            aboutToRemoveSharedObject(key, o);
        });
    }

    void ObjectPool::removeAllSharedObjects() {
        stdc_impl_t;
        eraseEverything(impl.sharedObjects, [this](std::string_view key, const NO<NamedObject> &o) {
            aboutToRemoveSharedObject(key, o);
        });
    }

    std::vector<NO<NamedObject>> ObjectPool::getSharedObjects(std::string_view id) const {
        stdc_impl_t;
        auto it = impl.sharedObjects.find(id);
        if (it == impl.sharedObjects.end()) {
            return {};
        }
        return it->second;
    }

    NO<NamedObject> ObjectPool::getFirstSharedObject(std::string_view id) const {
        stdc_impl_t;
        auto it = impl.sharedObjects.find(id);
        if (it == impl.sharedObjects.end()) {
            return {};
        }
        return it->second.front();
    }

    std::vector<NO<NamedObject>> ObjectPool::allSharedObjects() const {
        stdc_impl_t;
        std::vector<NO<NamedObject>> res;
        for (const auto &item : impl.sharedObjects) {
            res.insert(res.end(), item.second.begin(), item.second.end());
        }
        return res;
    }

    void ObjectPool::addUniqueObject(UNO<NamedObject> obj) {
        addUniqueObject({}, std::move(obj));
    }

    void ObjectPool::addUniqueObject(std::string_view id, UNO<NamedObject> obj) {
        stdc_impl_t;
        if (!obj) {
            return;
        }
        if (auto raw = addTo(impl.uniqueObjects, id, std::move(obj))) {
            uniqueObjectAdded(id, raw);
        }
    }

    void ObjectPool::removeUniqueObject(const NamedObject *obj) {
        removeUniqueObject({}, obj);
    }

    void ObjectPool::removeUniqueObject(std::string_view id, const NamedObject *obj) {
        stdc_impl_t;
        eraseOne(impl.uniqueObjects, id, obj,
                 [this](std::string_view key, const UNO<NamedObject> &o) {
                     aboutToRemoveUniqueObject(key, o.get());
                 });
    }

    void ObjectPool::removeUniqueObjects(std::string_view id) {
        stdc_impl_t;
        eraseUnder(impl.uniqueObjects, id, [this](std::string_view key, const UNO<NamedObject> &o) {
            aboutToRemoveUniqueObject(key, o.get());
        });
    }

    void ObjectPool::removeAllUniqueObjects() {
        stdc_impl_t;
        eraseEverything(impl.uniqueObjects,
                        [this](std::string_view key, const UNO<NamedObject> &o) {
                            aboutToRemoveUniqueObject(key, o.get());
                        });
    }

    std::vector<NamedObject *> ObjectPool::getUniqueObjects(std::string_view id) const {
        stdc_impl_t;
        std::vector<NamedObject *> res;
        auto it = impl.uniqueObjects.find(id);
        if (it == impl.uniqueObjects.end()) {
            return res;
        }
        res.reserve(it->second.size());
        for (const auto &item : it->second) {
            res.push_back(item.get());
        }
        return res;
    }

    NamedObject *ObjectPool::getFirstUniqueObject(std::string_view id) const {
        stdc_impl_t;
        auto it = impl.uniqueObjects.find(id);
        if (it == impl.uniqueObjects.end()) {
            return nullptr;
        }
        return it->second.front().get();
    }

    std::vector<NamedObject *> ObjectPool::allUniqueObjects() const {
        stdc_impl_t;
        std::vector<NamedObject *> res;
        for (const auto &item : impl.uniqueObjects) {
            for (const auto &entry : item.second) {
                res.push_back(entry.get());
            }
        }
        return res;
    }

    void ObjectPool::sharedObjectAdded(std::string_view id, const NO<NamedObject> &obj) {
        (void) id;
        (void) obj;
    }

    void ObjectPool::aboutToRemoveSharedObject(std::string_view id, const NO<NamedObject> &obj) {
        (void) id;
        (void) obj;
    }

    void ObjectPool::uniqueObjectAdded(std::string_view id, NamedObject *obj) {
        (void) id;
        (void) obj;
    }

    void ObjectPool::aboutToRemoveUniqueObject(std::string_view id, NamedObject *obj) {
        (void) id;
        (void) obj;
    }

    ObjectPool::ObjectPool(Impl &impl) : NamedObject(impl) {
    }

}
