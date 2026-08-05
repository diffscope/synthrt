#ifndef SYNTHRT_NAMEDOBJECT_H
#define SYNTHRT_NAMEDOBJECT_H

#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include <stdcorelib/adt/any.h>
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

        /// Arbitrary values kept alongside the object, by name.
        ///
        /// \note \c stdc::any rather than \c std::any. Objects here travel between the library and
        ///       the plugins that extend it, and the standard one identifies a type by \c typeid,
        ///       which two modules do not always agree on, and needs RTTI to do it.
        ///
        /// \sa stdc::any_cast()
        const stdc::any &property(std::string_view name) const;
        void setProperty(std::string_view name, stdc::any value);

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

    /// ObjectPool - Objects registered under a string, for parts of the program that have to find
    /// each other without knowing each other.
    ///
    /// Ownership is not one thing, so it is not one set. An object either has other owners besides
    /// the pool or it has none, and which it is decides what a lookup can hand back: a reference
    /// the caller may keep, or a pointer to borrow while the pool still holds it. The two sets are
    /// separate, so an identifier registered in one is not found in the other.
    class SYNTHRT_EXPORT ObjectPool : public NamedObject {
    public:
        explicit ObjectPool();
        ~ObjectPool();

    public:
        /// \name Shared objects
        ///
        /// Objects the pool holds a reference to alongside whoever else holds one. A lookup hands
        /// back a reference, so the object outlives the pool if a caller keeps it.
        /// @{
        void addSharedObject(const NO<NamedObject> &obj);
        void addSharedObject(std::string_view id, const NO<NamedObject> &obj);
        inline void addSharedObjects(std::string_view id, stdc::array_view<NO<NamedObject>> objs) {
            for (const auto &obj : objs) {
                addSharedObject(id, obj);
            }
        }
        void removeSharedObject(const NamedObject *obj);
        void removeSharedObject(std::string_view id, const NamedObject *obj);
        void removeSharedObjects(std::string_view id);
        void removeAllSharedObjects();

        std::vector<NO<NamedObject>> getSharedObjects(std::string_view id) const;
        NO<NamedObject> getFirstSharedObject(std::string_view id) const;
        std::vector<NO<NamedObject>> allSharedObjects() const;
        /// @}

        /// \name Unique objects
        ///
        /// Objects the pool alone owns. A lookup hands back a pointer to borrow, and removing one
        /// destroys it, so nothing can be left holding it afterwards.
        /// @{
        void addUniqueObject(UNO<NamedObject> obj);
        void addUniqueObject(std::string_view id, UNO<NamedObject> obj);
        void removeUniqueObject(const NamedObject *obj);
        void removeUniqueObject(std::string_view id, const NamedObject *obj);
        void removeUniqueObjects(std::string_view id);
        void removeAllUniqueObjects();

        std::vector<NamedObject *> getUniqueObjects(std::string_view id) const;
        NamedObject *getFirstUniqueObject(std::string_view id) const;
        std::vector<NamedObject *> allUniqueObjects() const;
        /// @}

    protected:
        /// \name Notifications
        ///
        /// Each pair carries what its own set can hand out, so an override sees the same ownership
        /// a lookup would have given it.
        /// @{
        virtual void sharedObjectAdded(std::string_view id, const NO<NamedObject> &obj);
        virtual void aboutToRemoveSharedObject(std::string_view id, const NO<NamedObject> &obj);

        virtual void uniqueObjectAdded(std::string_view id, NamedObject *obj);
        virtual void aboutToRemoveUniqueObject(std::string_view id, NamedObject *obj);
        /// @}

    protected:
        class Impl;
        explicit ObjectPool(Impl &impl);
    };

}

#endif // SYNTHRT_NAMEDOBJECT_H