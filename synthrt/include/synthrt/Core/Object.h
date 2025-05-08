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

        NO() = default;
        NO(T *obj) : Base(obj){};
        NO(const std::shared_ptr<T> &obj) : Base(obj){};
        NO(std::shared_ptr<T> &&obj) noexcept : Base(std::move(obj)){};
        template <class U>
        NO<U> as() const {
            return std::static_pointer_cast<U>(Base::operator*());
        }
    };

    class SYNTHRT_EXPORT Object : public NamedObject {
    public:
        explicit Object(Object *parent = nullptr);
        explicit Object(const std::string &name, Object *parent = nullptr);
        ~Object();

        const std::any &property(std::string_view name) const;
        void setProperty(std::string_view name, const std::any &value);

        Object *parent() const;
        void setParent(Object *parent);

    protected:
        class Impl;
        explicit Object(Impl &impl);
    };

    class SYNTHRT_EXPORT ObjectPool : public Object {
    public:
        explicit ObjectPool();
        virtual ~ObjectPool();

    public:
        void addObject(Object *obj);
        void addObject(std::string_view id, Object *obj);
        inline void addObjects(std::string_view id, stdc::array_view<Object *> objs) {
            for (const auto &obj : objs) {
                addObject(id, obj);
            }
        }
        void removeObject(Object *obj);
        void removeObject(std::string_view id, Object *obj);
        void removeObjects(std::string_view id);
        void removeAllObjects();

        std::vector<Object *> allObjects() const;
        std::vector<Object *> getObjects(std::string_view id) const;
        Object *getFirstObject(std::string_view id) const;

        /// If true, the object is automatically deleted when it is removed from the pool.
        bool autoDelete() const;
        void setAutoDelete(bool value);

    protected:
        virtual void objectAdded(std::string_view id, Object *obj);
        virtual void aboutToRemoveObject(std::string_view id, Object *obj);

    protected:
        class Impl;
        explicit ObjectPool(Impl &impl);
    };

}

#endif // OBJECT_H