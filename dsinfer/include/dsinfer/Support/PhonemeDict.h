#ifndef DSINFER_PHONEMEDICT_H
#define DSINFER_PHONEMEDICT_H

#include <memory>
#include <filesystem>
#include <system_error>
#include <cstring>
#include <optional>
#include <vector>

#include <stdcorelib/stlextra/iterator.h>
#include <dsinfer/dsinfer_global.h>

namespace ds {

    class PhonemeDict;

    /// PhonemeList stores a sequence of phonemes where each element is a null-terminated string.
    /// The sequence maintains contiguous memory storage of the original input format.
    class PhonemeList {
    public:
        inline PhonemeList() noexcept : _data(nullptr), _count(0) {
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
            return iterator(nullptr, static_cast<int>(_count));
        }

        template <class T = std::string_view>
        std::vector<T> vec() const {
            return std::vector<T>(begin(), end());
        }

    protected:
        PhonemeList(const char *data, uint32_t count) : _data(data), _count(count) {
        }

        const char *_data;
        uint32_t _count;

        friend class PhonemeDict;
    };

    /// PhonemeDict is a constant container that maps phoneme name to a sequence of phonemes, which
    /// focuses on efficiency and memory usage.
    class DSINFER_EXPORT PhonemeDict {
    public:
        using key_type = const char *;
        using mapped_type = PhonemeList;
        using value_type = std::pair<const char *, PhonemeList>;
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

        /// Loads a pronunciation lexicon into a memory-mapped hash table.
        ///
        /// Reads a text file where each line contains:
        ///     \c [WORD]\t[PHONEME_SEQUENCE]
        /// The phoneme sequence is a space-separated list of strings.
        ///
        /// Example line : "HELLO\tHH AH L OW\n"
        bool load(const std::filesystem::path &path, std::error_code *ec);

    public:
        class iterator {
        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type = PhonemeDict::value_type;
            using difference_type = ptrdiff_t;
            using pointer = const value_type *;
            using reference = const value_type &;

            inline iterator() : _buf(nullptr), _row(nullptr), _col(nullptr) {
            }

        public:
            inline reference operator*() const {
                fetch();
                return _copy.value();
            }
            inline pointer operator->() const {
                fetch();
                return &_copy.value();
            }
            inline iterator &operator++() {
                next();
                return *this;
            }
            inline iterator operator++(int) {
                auto tmp = *this;
                ++*this;
                return tmp;
            }
            inline iterator &operator--() {
                prev();
                return *this;
            }
            inline iterator operator--(int) {
                auto tmp = *this;
                --*this;
                return tmp;
            }
            inline bool operator==(const iterator &RHS) const {
                return equals(RHS);
            }
            inline bool operator!=(const iterator &RHS) const {
                return !(*this == RHS);
            }

        private:
            inline iterator(const char *buf, const void *row, const void *col)
                : _buf(buf), _row(row), _col(col) {
            }

            DSINFER_EXPORT void fetch() const;
            DSINFER_EXPORT void next();
            DSINFER_EXPORT void prev();
            DSINFER_EXPORT bool equals(const iterator &RHS) const;

            const char *_buf;
            const void *_row, *_col;
            mutable std::optional<std::pair<const char *, PhonemeList>> _copy;

            friend class PhonemeDict;
        };

        using reverse_iterator = stdc::reverse_iterator<iterator>;

        /// \note The key must be a null-terminated string.
        iterator find(const char *key) const;
        bool contains(const char *key) const;
        PhonemeList operator[](const char *key) const;

        bool empty() const;
        size_t size() const;

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