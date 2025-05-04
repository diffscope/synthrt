#ifndef DSINFER_PHONEMEDICT_H
#define DSINFER_PHONEMEDICT_H

#include <memory>
#include <filesystem>
#include <system_error>
#include <cstring>

#include <stdcorelib/stlextra/iterator.h>
#include <dsinfer/dsinfer_global.h>

namespace ds {

    class PhonemeDict;

    class PhonemeList {
    public:
        PhonemeList() noexcept : _data(nullptr), _count(0) {
        }

        class iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = const char *;
            using difference_type = int;
            using pointer = const value_type *;
            using reference = const value_type &;

            inline iterator() noexcept : _data(nullptr), _index(0) {
            }

            inline reference operator*() const noexcept {
                return _data;
            }

            inline pointer operator->() const noexcept {
                return &_data;
            }

            inline iterator &operator++() {
                _data += std::strlen(_data) + 1; // move to next word
                ++_index;
                return *this;
            }

            inline iterator operator++(int) {
                auto tmp = *this;
                ++*this;
                return tmp;
            }

            inline bool operator==(const iterator &RHS) const noexcept {
                return _index == RHS._index;
            }

            inline bool operator!=(const iterator &RHS) const noexcept {
                return !(*this == RHS);
            }

        private:
            inline iterator(const char *key, int index) noexcept : _data(key), _index(index) {
            }

            const char *_data;
            int _index;

            friend class PhonemeList;
        };

        iterator begin() const noexcept {
            return iterator(_data, 0);
        }
        iterator end() const noexcept {
            return iterator(nullptr, _count);
        }
        std::vector<const char *> vec() {
            return std::vector<const char *>(begin(), end());
        }

    protected:
        PhonemeList(const char *data, int count) : _data(data), _count(count) {
        }

        const char *_data;
        int _count;

        friend class PhonemeDict;
    };

    /// PhonemeDict is a constant container that maps phoneme name to a list of phonemes, which
    /// focuses on efficiency and memory usage.
    class DSINFER_EXPORT PhonemeDict {
    public:
        class iterator;

        class Value {
        public:
            Value() : _key(), _value() {
            }
            Value(const char *key, const PhonemeList &value) : _key(key), _value(value) {
            }
            const char *key() const {
                return _key;
            }
            PhonemeList value() const {
                return _value;
            }

        private:
            const char *_key;
            PhonemeList _value;
        };

        class ValueRef {
        public:
            ValueRef() : _buf(nullptr), _row(nullptr), _col(nullptr) {
            }

            DSINFER_EXPORT operator Value() const;

            const char *key() const {
                return operator Value().key();
            }
            PhonemeList value() const {
                return operator Value().value();
            }

            DSINFER_EXPORT ValueRef &operator++();
            DSINFER_EXPORT ValueRef &operator--();

            DSINFER_EXPORT bool operator==(const ValueRef &RHS) const;
            inline bool operator!=(const ValueRef &RHS) const {
                return !(*this == RHS);
            }

        private:
            ValueRef(const char *buf, const void *row, const void *col)
                : _buf(buf), _row(row), _col(col) {
            }
            const char *_buf;
            const void *_row, *_col;

            friend class iterator;
            friend class PhonemeDict;
        };

    public:
        using key_type = const char *;
        using mapped_type = PhonemeList;
        using value_type = Value;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        // using allocator_type = ??; // implementation detail, not exposed
        using reference = const value_type &;
        using const_reference = const value_type &;
        using pointer = const value_type *;
        using const_pointer = const value_type *;

    public:
        PhonemeDict();
        ~PhonemeDict();

        bool load(const std::filesystem::path &path, std::error_code *ec);

    public:
        class iterator {
        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type = PhonemeDict::value_type;
            using difference_type = ptrdiff_t;
            using pointer = ValueRef *;
            using reference = ValueRef &;

            iterator() = default;

        public:
            inline reference operator*() const {
                return _ref;
            }
            inline pointer operator->() const {
                return &_ref;
            }
            inline iterator &operator++() {
                ++_ref;
                return *this;
            }
            inline iterator operator++(int) {
                auto tmp = *this;
                ++*this;
                return tmp;
            }
            inline iterator &operator--() {
                --_ref;
                return *this;
            }
            inline iterator operator--(int) {
                auto tmp = *this;
                --*this;
                return tmp;
            }
            bool operator==(const iterator &RHS) const {
                return _ref == RHS._ref;
            }
            bool operator!=(const iterator &RHS) const {
                return !(*this == RHS);
            }

        private:
            iterator(ValueRef ref) : _ref(ref) {
            }

            mutable ValueRef _ref;

            friend class PhonemeDict;
        };

        using reverse_iterator = stdc::reverse_iterator<iterator>;

        iterator find(const char *key) const; // only accept null-terminated string
        bool empty() const;
        size_t size() const;
        bool contains(const char *key) const;
        PhonemeList operator[](const char *key) const;

        iterator begin() const;
        iterator end() const;
        inline reverse_iterator rbegin() const {
            return reverse_iterator(end());
        }
        inline reverse_iterator rend() const {
            return reverse_iterator(begin());
        }

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

}


#endif // DSINFER_PHONEMEDICT_H