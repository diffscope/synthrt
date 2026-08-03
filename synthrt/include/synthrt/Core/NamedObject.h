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
        using Base::Base;

        constexpr NO() noexcept = default;

        NO(const Base &RHS) noexcept : Base(RHS) {
        }

        NO(Base &&RHS) noexcept : Base(std::move(RHS)) {
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

    /// UNO - A unique pointer wrapper for \c NamedObject instance.
    ///
    /// The counterpart of \c NO for the ordinary case of a single owner, which most objects here
    /// are. Reach for \c NO only where an object genuinely outlives one owner, as a tensor handed
    /// from one inference stage to the next does.
    ///
    /// An \c UNO converts to an \c NO by moving from it, so a factory can hand out unique
    /// ownership without deciding on its callers' behalf whether they will need to share.
    ///
    /// \code
    ///     UNO<Tensor> owned = UNO<Tensor>::create();
    ///     NO<ITensor> shared = std::move(owned);      // ownership moves into the shared pointer
    /// \endcode
    template <class T>
    class UNO : public std::unique_ptr<T> {
        static_assert(std::is_base_of<NamedObject, T>::value,
                      "T should inherit from srt::NamedObject");

    public:
        using Base = std::unique_ptr<T>;
        using Base::Base;

        constexpr UNO() noexcept = default;

        UNO(Base &&RHS) noexcept : Base(std::move(RHS)) {
        }

        /// Returns the pointee seen as \a U. Ownership stays here, unlike \c NO::as().
        template <class U>
        U *as() const noexcept {
            return static_cast<U *>(this->get());
        }

        template <class... Args>
        static UNO<T> create(Args &&...args) {
            return UNO<T>(new T(std::forward<Args>(args)...));
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