#ifndef SYNTHRT_NAMEDOBJECT_H
#define SYNTHRT_NAMEDOBJECT_H

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
        explicit NamedObject(std::string name);
        virtual ~NamedObject();

        const std::string &objectName() const;
        void setObjectName(std::string name);

        const std::any &property(std::string_view name) const;
        void setProperty(std::string_view name, std::any value);

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
        explicit NamedObject(Impl &impl);
    };

    /// NO - A shared pointer wrapper for \c NamedObject instance.
    template <class T>
    class NO : public std::shared_ptr<T> {
        static_assert(std::is_base_of<NamedObject, T>::value,
                      "T should inherit from srt::NamedObject");

    public:
        using Base = std::shared_ptr<T>;

        template <typename... Args>
        using Constructible = typename std::enable_if_t<std::is_constructible_v<Base, Args...>>;

        constexpr NO() noexcept : Base() {
        }

        NO(const Base &RHS) noexcept : Base(RHS) {
        }

        template <typename U, typename = Constructible<U *>>
        explicit NO(U *p) : Base(p) {
        }

        template <typename U, typename Deleter, typename = Constructible<U *, Deleter>>
        NO(U *p, Deleter d) : Base(p, std::move(d)) {
        }

        template <typename Deleter>
        NO(std::nullptr_t p, Deleter d) : Base(p, std::move(d)) {
        }

        template <typename U, typename Deleter, typename Alloc,
                  typename = Constructible<U *, Deleter, Alloc>>
        NO(U *p, Deleter d, Alloc a) : Base(p, std::move(d), std::move(a)) {
        }

        template <typename Deleter, typename Alloc>
        NO(std::nullptr_t p, Deleter d, Alloc a) : Base(p, std::move(d), std::move(a)) {
        }

        template <typename U>
        NO(const std::shared_ptr<U> &RHS, T *p) noexcept : Base(RHS, p) {
        }

        template <typename U, typename = Constructible<const std::shared_ptr<U> &>>
        NO(const std::shared_ptr<U> &RHS) noexcept : Base(RHS) {
        }

        NO(Base &&RHS) noexcept : Base(std::move(RHS)) {
        }

        template <typename U, typename = Constructible<std::shared_ptr<U>>>
        NO(Base &&RHS) noexcept : Base(std::move(RHS)) {
        }

        template <typename U, typename = Constructible<const std::weak_ptr<U> &>>
        explicit NO(const std::weak_ptr<U> &RHS) : Base(RHS) {
        }

        template <typename U, typename Deleter,
                  typename = Constructible<std::unique_ptr<U, Deleter>>>
        NO(std::unique_ptr<U, Deleter> &&RHS) : Base(std::move(RHS)) {
        }

        constexpr NO(std::nullptr_t) noexcept : Base() {
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

#endif // SYNTHRT_NAMEDOBJECT_H