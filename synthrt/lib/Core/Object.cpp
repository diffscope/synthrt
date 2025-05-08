#include "Object.h"
#include "Object_p.h"

#include <stdcorelib/pimpl.h>

namespace srt {

    Object::Impl::Impl(Object *decl) : NamedObject::Impl(decl) {
    }

    Object::Impl::~Impl() = default;

    void Object::Impl::deleteChildren() {
        for (int i = 0; i < children.size(); ++i) {
            currentChildBeingDeleted = children.at(i);
            children[i] = nullptr;
            delete currentChildBeingDeleted;
        }
        children.clear();
        currentChildBeingDeleted = nullptr;
        isDeletingChildren = false;
    }

    void Object::Impl::setParent(Object *o) {
        if (o == parent)
            return;

        if (parent) {
            auto parentD = static_cast<Impl *>(parent->_impl.get());
            if (parentD->isDeletingChildren && wasDeleted &&
                parentD->currentChildBeingDeleted == _decl) {
                // don't do anything since deleteChildren() already
                // cleared our entry in parentD->children.
            } else {
                auto it = std::find(children.begin(), children.end(), o);
                if (it != children.end()) {
                    // we're probably recursing into setParent() from a ChildRemoved event, don't
                    // do anything
                } else if (parentD->isDeletingChildren) {
                    *it = nullptr;
                } else {
                    parentD->children.erase(it);
                }
            }
        }

        parent = o;

        if (parent) {
            auto parentD = static_cast<Impl *>(parent->_impl.get());
            parentD->children.push_back(static_cast<Object *>(_decl));
        }
    }

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

    Object::Object(Object *parent) : Object(*new Impl(this)) {
        setParent(parent);
    }

    Object::Object(const std::string &name, Object *parent) : Object(parent) {
        setObjectName(name);
    }

    Object::~Object() {
        __stdc_impl_t;
        impl.wasDeleted = true;
        if (!impl.children.empty())
            impl.deleteChildren();

        if (impl.parent) // remove it from parent object
            impl.setParent(nullptr);
    }

    static std::any &staticEmptyObjectProperty() {
        static std::any empty;
        return empty;
    }

    const std::any &Object::property(std::string_view name) const {
        __stdc_impl_t;
        auto it = impl.properties.find(name);
        if (it == impl.properties.end()) {
            return staticEmptyObjectProperty();
        }
        return it->second;
    }

    void Object::setProperty(std::string_view name, const std::any &value) {
        __stdc_impl_t;
        impl.properties[std::string(name)] = value;
    }

    Object *Object::parent() const {
        __stdc_impl_t;
        return impl.parent;
    }

    void Object::setParent(Object *parent) {
        __stdc_impl_t;
        impl.setParent(parent);
    }

    Object::Object(Impl &impl) : NamedObject(impl) {
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

    void ObjectPool::addObject(Object *obj) {
        addObject({}, obj);
    }

    void ObjectPool::addObject(std::string_view id, Object *obj) {
        __stdc_impl_t;

        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            it = impl.objects.emplace(std::string(id), stdc::linked_map<Object *, int>()).first;
        }
        it->second.append(obj, {});
    }

    void ObjectPool::removeObject(Object *obj, bool del) {
        removeObject({}, obj, del);
    }

    void ObjectPool::removeObject(std::string_view id, Object *obj, bool del) {
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
            aboutToRemoveObject(id, it2->first);
            if (del) {
                delete it2->first;
            }
            map.erase(it2);
            if (map.empty()) {
                impl.objects.erase(it);
            }
        }
    }

    void ObjectPool::removeObjects(std::string_view id, bool del) {
        __stdc_impl_t;
        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            return;
        }
        auto &map = it->second;
        for (auto it = map.rbegin(); it != map.rend(); ++it) {
            aboutToRemoveObject(id, it->first);
            if (del) {
                delete it->first;
            }
        }
        impl.objects.erase(it);
    }

    void ObjectPool::removeAllObjects(bool del) {
        __stdc_impl_t;
        for (auto &item : impl.objects) {
            auto &map = item.second;
            for (auto it = map.rbegin(); it != map.rend(); ++it) {
                aboutToRemoveObject(item.first, it->first);
                if (del) {
                    delete it->first;
                }
            }
        }
        impl.objects.clear();
    }

    std::vector<Object *> ObjectPool::allObjects() const {
        __stdc_impl_t;
        std::vector<Object *> res;
        for (const auto &item : impl.objects) {
            auto keys = item.second.keys();
            res.insert(res.end(), keys.begin(), keys.end());
        }
        return res;
    }

    std::vector<Object *> ObjectPool::getObjects(std::string_view id) const {
        __stdc_impl_t;
        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            return {};
        }
        return it->second.keys();
    }

    Object *ObjectPool::getFirstObject(std::string_view id) const {
        __stdc_impl_t;
        auto it = impl.objects.find(id);
        if (it == impl.objects.end()) {
            return {};
        }
        return it->second.begin()->first;
    }

    bool ObjectPool::autoDelete() const {
        __stdc_impl_t;
        return impl.autoDelete;
    }

    void ObjectPool::setAutoDelete(bool value) {
        __stdc_impl_t;
        impl.autoDelete = value;
    }

    void ObjectPool::objectAdded(std::string_view id, Object *obj) {
        (void) id;
        (void) obj;
    }

    void ObjectPool::aboutToRemoveObject(std::string_view id, Object *obj) {
        (void) id;
        (void) obj;
    }

    ObjectPool::ObjectPool(Impl &impl) : Object(impl) {
    }

}