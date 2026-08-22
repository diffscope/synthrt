#pragma once

#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

#include <stdcorelib/stlextra/iterator.h>

#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p
{

    class PhonemeDict;

    /// PhonemeList stores a sequence of phonemes where each element is a null-terminated string.
    /// The sequence maintains contiguous memory storage of the original input format.
    class PhonemeList {
    public:
        PhonemeList() noexcept : _data(nullptr), _count(0) {}

        class iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = const char *;
            using difference_type = int;
            using pointer = const value_type *;
            using reference = const value_type &;

            iterator() noexcept : _data(nullptr), _index(0) {}
            reference operator*() const noexcept { return _data; }
            pointer operator->() const noexcept { return &_data; }
            iterator &operator++() {
                _data += std::strlen(_data) + 1; // move to next word
                ++_index;
                return *this;
            }
            iterator operator++(int) {
                const auto tmp = *this;
                ++*this;
                return tmp;
            }
            bool operator==(const iterator &RHS) const noexcept { return _index == RHS._index; }
            bool operator!=(const iterator &RHS) const noexcept { return !(*this == RHS); }

        private:
            iterator(const char *key, const int index) noexcept : _data(key), _index(index) {}
            const char *_data;
            int _index;

            friend class PhonemeList;
        };

        iterator begin() const noexcept { return iterator(_data, 0); }
        iterator end() const noexcept { return iterator(nullptr, static_cast<int>(_count)); }

        template <class T = std::string_view>
        std::vector<T> vec() const {
            return std::vector<T>(begin(), end());
        }

    protected:
        PhonemeList(const char *data, const uint32_t count) : _data(data), _count(count) {}

        const char *_data;
        uint32_t _count;

        friend class PhonemeDict;
    };

    /// PhonemeDict is a constant container that maps phoneme name to a sequence of phonemes, which
    /// focuses on efficiency and memory usage.
    class SRT_G2P_EXPORT PhonemeDict {
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

        PhonemeDict();
        ~PhonemeDict();

        // Internal map stores char* pointers into filebuf memory.
        // Copying/moving would create dangling pointers into a different buffer.
        PhonemeDict(const PhonemeDict &) = delete;
        PhonemeDict &operator=(const PhonemeDict &) = delete;
        PhonemeDict(PhonemeDict &&) = delete;
        PhonemeDict &operator=(PhonemeDict &&) = delete;

        void reset();

        /// Loads a pronunciation lexicon into a memory-mapped hash table.
        ///
        /// Reads a text file where each line contains:
        ///     \c [WORD]\t[PHONEME_SEQUENCE]
        /// The phoneme sequence is a space-separated list of strings.
        ///
        /// Example line : "HELLO\tHH AH L OW\n"
        bool load(const std::filesystem::path &path, std::error_code *ec);

        class iterator {
        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type = value_type;
            using difference_type = ptrdiff_t;
            using pointer = const value_type *;
            using reference = const value_type &;

            iterator() : _buf(nullptr), _row(nullptr), _col(nullptr), _var(0) {}

            reference operator*() const {
                fetch();
                return _copy.value();
            }
            pointer operator->() const {
                fetch();
                return &_copy.value();
            }
            iterator &operator++() {
                next();
                return *this;
            }
            iterator operator++(int) {
                auto tmp = *this;
                ++*this;
                return tmp;
            }
            iterator &operator--() {
                prev();
                return *this;
            }
            iterator operator--(int) {
                auto tmp = *this;
                --*this;
                return tmp;
            }
            bool operator==(const iterator &RHS) const { return equals(RHS); }
            bool operator!=(const iterator &RHS) const { return !(*this == RHS); }

        private:
            iterator(const char *buf, const void *row, const void *col, int var = 0)
                : _buf(buf), _row(row), _col(col), _var(var) {}

            SRT_G2P_EXPORT void fetch() const;
            SRT_G2P_EXPORT void next();
            SRT_G2P_EXPORT void prev();
            SRT_G2P_EXPORT bool equals(const iterator &RHS) const;

            const char *_buf;
            const void *_row, *_col;
            // Variant index within the merged entry group. 0 for plain keys and
            // range iterators; set by find() when a suffixed key ("word(2)")
            // resolves to a specific variant. Reset to 0 by next()/prev().
            int _var;
            mutable std::optional<std::pair<const char *, PhonemeList>> _copy;

            friend class PhonemeDict;
        };

        using reverse_iterator = stdc::reverse_iterator<iterator>;

        /// \note The key must be a null-terminated string.
        ///
        /// Dictionaries may carry CMU-style variant keys like "word(1)"/"word(2)":
        /// the "(n)" suffix is stripped at load time and all rows sharing the same
        /// base word are merged into one variant group (bare base row first, then
        /// variants in file order). find/contains/operator[] on a base key return
        /// the first variant; on a suffixed key they fall back to the variant group
        /// and return exactly the variant whose suffix number matches (D4 decision:
        /// precise suffixed lookup returns that single pronunciation, no candidates).
        iterator find(const char *key) const;
        bool contains(const char *key) const;
        PhonemeList operator[](const char *key) const;

        /// Returns every pronunciation of a (base) key in file order, as
        /// lightweight views into the dictionary buffer (zero-copy).
        /// For a suffixed key like "word(2)", returns only the matching variant.
        std::vector<PhonemeList> lookupAll(const char *key) const;

        bool empty() const;
        size_t size() const;

        iterator begin() const;
        iterator end() const;
        reverse_iterator rbegin() const { return reverse_iterator(end()); }
        reverse_iterator rend() const { return reverse_iterator(begin()); }

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

} // namespace srt::g2p
