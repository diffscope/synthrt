#ifndef OBJECT_H
#define OBJECT_H

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <any>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/synthrt_global.h>

namespace srt {

    class SYNTHRT_EXPORT NamedObject {
    public:
        NamedObject();
        explicit NamedObject(const std::string &name);
        virtual ~NamedObject();

        const std::string &objectName() const;
        void setObjectName(const std::string &name);
        void setObjectName(std::string &&name);

        const std::any &property(std::string_view name) const;
        void setProperty(std::string_view name, const std::any &value);
        void setProperty(std::string_view name, std::any &&value);

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
        explicit NamedObject(Impl &impl);
    };

    /// A shared wrapper for \a NamedObject.
    template <class T>
    class NO : public std::shared_ptr<T> {
        static_assert(std::is_base_of<NamedObject, T>::value,
                      "T should inherit from srt::NamedObject");

    public:
        using Base = std::shared_ptr<T>;

        template <class... Args>
        NO(Args &&...args) : Base(std::forward<Args>(args)...) {
        }
        template <class U>
        NO<U> as() const noexcept {
            return std::static_pointer_cast<U>(*this);
        }
        template <class... Args>
        static NO<T> create(Args &&...args) {
            return std::make_shared<T>(std::forward<Args>(args)...);
        }
    };

    class SYNTHRT_EXPORT ObjectPool : public NamedObject {
    public:
        explicit ObjectPool();
        ~ObjectPool();

    public:
        void addObject(const NO<NamedObject> &obj);
        void addObject(std::string_view id, const NO<NamedObject> &obj);
        inline void addObjects(std::string_view id, stdc::array_view<NO<NamedObject>> objs) {
            for (const auto &obj : objs) {
                addObject(id, obj);
            }
        }
        void removeObject(const NamedObject *obj);
        void removeObject(std::string_view id, const NamedObject *obj);
        void removeObjects(std::string_view id);
        void removeAllObjects();

        std::vector<NO<NamedObject>> allObjects() const;
        std::vector<NO<NamedObject>> getObjects(std::string_view id) const;
        NO<NamedObject> getFirstObject(std::string_view id) const;

    protected:
        virtual void objectAdded(std::string_view id, const NO<NamedObject> &obj);
        virtual void aboutToRemoveObject(std::string_view id, const NO<NamedObject> &obj);

    protected:
        class Impl;
        explicit ObjectPool(Impl &impl);
    };

}

#endif // OBJECT_H