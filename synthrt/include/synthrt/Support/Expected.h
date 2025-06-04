#ifndef SYNTHRT_EXPECTED_H
#define SYNTHRT_EXPECTED_H

#include <type_traits>
#include <optional>
#include <cassert>

#include <synthrt/Support/Error.h>

namespace srt {

    /// Tagged union holding either a T or an Error.
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
        /// Create an Expected<T> error value from the given Error.
        Expected(Error err) {
            assert(!err.ok() && "Cannot create Expected<T> from Error success value.");
            _err = err;
        }

        /// Create an Expected<T> success value from the given U value, which
        /// must be convertible to T.
        template <typename U>
        Expected(U &&val, typename std::enable_if_t<std::is_convertible_v<U, T>> * = nullptr) {
            _val = value_type(std::forward<U>(val));
        }

        /// Move construct an Expected<T> value.
        Expected(Expected &&RHS) = default;

        /// Move construct an Expected<T> value from an Expected<U>, where U
        /// must be convertible to T.
        template <class U>
        Expected(Expected<U> &&RHS,
                 typename std::enable_if_t<std::is_convertible_v<U, T>> * = nullptr) {
            if (RHS.hasValue())
                _val = std::move(RHS._val);
            else
                _err = std::move(RHS._err);
        }

        /// Move construct an Expected<T> value from an Expected<U>, where U
        /// isn't convertible to T.
        template <class U>
        explicit Expected(Expected<U> &&RHS,
                          typename std::enable_if_t<!std::is_convertible_v<U, T>> * = nullptr) {
            if (RHS.hasValue())
                _val = std::move(RHS._val);
            else
                _err = std::move(RHS._err);
        }

        Expected &operator=(Expected &&RHS) = default;

        ~Expected() = default;

        explicit operator bool() {
            return hasValue();
        }
        reference get() {
            assert(hasValue() && "Expected doesn't contain a value");
            return _val.value();
        }
        const_reference get() const {
            assert(hasValue() && "Expected doesn't contain a value");
            return _val.value();
        }

        /// Take ownership of the stored error.
        /// After calling this the Expected<T> is in an indeterminate state that can
        /// only be safely destructed. No further calls (beside the destructor) should
        /// be made on the Expected<T> value.
        Error takeError() {
            return _err ? Error(std::move(_err.value())) : Error::success();
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
            return _val.has_value();
        }

        const Error &error() const & {
            assert(_err.has_value() && "Expected doesn't contain an error");
            return _err.value();
        }

        T orElse(const T &defaultValue) const {
            return _val.has_value() ? _val.value() : defaultValue;
        }

        T orElse(T &&defaultValue) const {
            return _val.has_value() ? std::move(_val.value()) : std::move(defaultValue);
        }

    protected:
        std::optional<value_type> _val;
        std::optional<error_type> _err;
    };

}

#endif // SYNTHRT_EXPECTED_H