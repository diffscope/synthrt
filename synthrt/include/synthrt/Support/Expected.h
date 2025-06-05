#ifndef SYNTHRT_EXPECTED_H
#define SYNTHRT_EXPECTED_H

#include <cassert>
#include <type_traits>

#include <synthrt/Support/Error.h>

namespace srt {

    /// Expected - A simple re-implementation of \a llvm::Expected. Tagged union holding either
    /// a T or an Error.
    template <class T>
    class Expected {
        static_assert(!std::is_reference_v<T>, "T must not be a reference");
        static_assert(!std::is_same_v<T, Error>, "T must not be Error");

        template <class U>
        friend class Expected;

    public:
        using value_type = T;
        using error_type = Error;

    private:
        using reference = typename std::remove_reference_t<T> &;
        using const_reference = const typename std::remove_reference_t<T> &;
        using pointer = typename std::remove_reference_t<T> *;
        using const_pointer = const typename std::remove_reference_t<T> *;

    public:
        Expected() : _has_value(true) {
            new (&_storage.val) value_type(value_type{});
        }

        /// Create an Expected<T> error value from the given Error.
        Expected(Error err) : _has_value(false) {
            assert(!err.ok() && "Cannot create Expected<T> from Error success value.");
            new (&_storage) error_type(std::move(err));
        }

        /// Create an Expected<T> success value from the given U value, which
        /// must be convertible to T.
        template <typename U>
        Expected(U &&val, typename std::enable_if_t<std::is_convertible_v<U, T>> * = nullptr)
            : _has_value(true) {
            new (&_storage.val) value_type(std::forward<U>(val));
        }

        /// Move construct an Expected<T> value.
        Expected(Expected &&RHS) {
            moveConstruct(std::move(RHS));
        }

        /// Move construct an Expected<T> value from an Expected<U>, where U
        /// must be convertible to T.
        template <class U>
        Expected(Expected<U> &&RHS,
                 typename std::enable_if_t<std::is_convertible_v<U, T>> * = nullptr) {
            moveConstruct(std::move(RHS));
        }

        /// Move construct an Expected<T> value from an Expected<U>, where U
        /// isn't convertible to T.
        template <class U>
        explicit Expected(Expected<U> &&RHS,
                          typename std::enable_if_t<!std::is_convertible_v<U, T>> * = nullptr) {
            moveConstruct(std::move(RHS));
        }

        Expected &operator=(Expected &&RHS) {
            moveAssign(std::move(RHS));
            return *this;
        }

        ~Expected() {
            if (_has_value) {
                if constexpr (!std::is_trivially_destructible_v<T>)
                    _storage.val.~value_type();
            } else
                _storage.err.~error_type();
        }

        explicit operator bool() {
            return _has_value;
        }
        reference get() {
            assert(_has_value && "Expected doesn't contain a value");
            return _storage.val;
        }
        const_reference get() const {
            assert(_has_value && "Expected doesn't contain a value");
            return _storage.val;
        }

        /// Take ownership of the stored error.
        /// After calling this the Expected<T> is in an indeterminate state that can
        /// only be safely destructed. No further calls (beside the destructor) should
        /// be made on the Expected<T> value.
        error_type takeError() {
            return _has_value ? Error::success() : std::move(_storage.err);
        }

        pointer operator->() {
            return &get();
        }
        const_pointer operator->() const {
            return &get();
        }
        reference operator*() {
            return get();
        }
        const_reference operator*() const {
            return get();
        }

        // Extra APIs
        bool hasValue() const {
            return _has_value;
        }

        T &&take() {
            return std::move(get());
        }

        const error_type &error() const & {
            assert(!_has_value && "Expected doesn't contain an error");
            return _storage.err;
        }

        T orElse(const T &defaultValue) const {
            return _has_value ? _storage.val : defaultValue;
        }

        T orElse(T &&defaultValue) const {
            return _has_value ? std::move(_storage.val) : std::move(defaultValue);
        }

    protected:
        template <class U>
        void moveConstruct(Expected<U> &&RHS) {
            _has_value = RHS._has_value;
            if (_has_value)
                new (&_storage.val) value_type(std::move(RHS._storage.val));
            else
                new (&_storage.err) error_type(std::move(RHS._storage.err));
        }

        template <class U>
        void moveAssign(Expected<U> &&RHS) {
            if constexpr (std::is_same_v<value_type, U>) {
                if (&RHS == this)
                    return;
            }
            this->~Expected();
            new (this) Expected(std::move(RHS));
        }

        union Storage {
            value_type val;
            error_type err;
            char no_init;

            // Do nothing in constructor and destructor.
            Storage(){};
            ~Storage(){};
        };
        Storage _storage;
        bool _has_value : 1;
    };

    // Specialization for Expected<void>
    template <>
    class Expected<void> {
        template <class U>
        friend class Expected;

    public:
        using value_type = void;
        using error_type = Error;

    public:
        Expected() : _has_value(true) {
        }

        /// Create an Expected<T> error value from the given Error.
        Expected(Error err) : _has_value(false) {
            assert(!err.ok() && "Cannot create Expected<T> from Error success value.");
            new (&_storage) error_type(std::move(err));
        }

        Expected(Expected &&RHS) {
            moveConstruct(std::move(RHS));
        }

        template <class U>
        Expected(Expected<U> &&RHS) {
            moveConstruct(std::move(RHS));
        }

        Expected &operator=(Expected &&RHS) {
            moveAssign(std::move(RHS));
            return *this;
        }

        ~Expected() {
            if (!_has_value)
                _storage.err.~error_type();
        }

        explicit operator bool() {
            return _has_value;
        }

        error_type takeError() {
            return _has_value ? Error::success() : std::move(_storage.err);
        }

        // Extra APIs
        bool hasValue() const {
            return _has_value;
        }

        const error_type &error() const & {
            assert(!_has_value && "Expected doesn't contain an error");
            return _storage.err;
        }

    protected:
        template <class U>
        void moveConstruct(Expected<U> &&RHS) {
            _has_value = RHS._has_value;
            if (!_has_value)
                new (&_storage.err) error_type(std::move(RHS._storage.err));
        }

        template <class U>
        void moveAssign(Expected<U> &&RHS) {
            if constexpr (std::is_same_v<value_type, U>) {
                if (&RHS == this)
                    return;
            }
            this->~Expected();
            new (this) Expected(std::move(RHS));
        }

        union Storage {
            error_type err;
            char no_init;

            // Do nothing in constructor and destructor.
            Storage(){};
            ~Storage(){};
        };
        Storage _storage;
        bool _has_value : 1;
    };

}

#endif // SYNTHRT_EXPECTED_H