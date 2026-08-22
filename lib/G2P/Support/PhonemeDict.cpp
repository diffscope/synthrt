#include <sparsepp/spp.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <synthrt/G2P/Support/PhonemeDict.h>

#include <cctype>
#include <cstring>
#include <fstream>
#include <string>

namespace srt::g2p {

    /// Matches a CMU-style trailing "(digits)" variant suffix in \p key.
    /// On match returns true and sets \p pos to the index of '(' and \p no to
    /// the parsed variant number. Requires at least one base character before
    /// '(' and at least one digit; keys like "word(x)" or "(1)" never match.
    static bool parse_variant_suffix(const char *key, size_t len, size_t &pos, uint32_t &no) {
        if (len < 4 || key[len - 1] != ')') // minimum form: "a(1)"
            return false;
        size_t i = len - 2;
        if (!std::isdigit(static_cast<unsigned char>(key[i])))
            return false;
        while (i > 0 && std::isdigit(static_cast<unsigned char>(key[i])))
            --i;
        if (i == 0 || key[i] != '(')
            return false;
        if (len - 2 - i > 9) // digit run too long, avoid uint32 overflow
            return false;
        uint32_t n = 0;
        for (size_t j = i + 1; j <= len - 2; ++j)
            n = n * 10 + static_cast<uint32_t>(key[j] - '0');
        pos = i;
        no  = n;
        return true;
    }

    static std::error_code make_last_error() {
#ifdef _WIN32
        return std::error_code(errno, stdc::windows_utf8_category());
#else
        return std::error_code(errno, std::system_category());
#endif
    }

    struct const_char_hash {
        size_t operator()(const char *key) const noexcept {
            return spp::spp_hash<std::string_view>()(std::string_view(key, std::strlen(key)));
        }
    };

    struct const_char_equal {
        bool operator()(const char *key1, const char *key2) const noexcept {
            return std::strcmp(key1, key2) == 0;
        }
    };

    class PhonemeDict::Impl {
    public:
        struct Entry {
            uint32_t offset;
            uint32_t count;
            uint32_t suffix; // 0 = bare base row, n = "(n)" variant number
        };
        // Rows sharing a base word (after "(n)" suffix stripping) merge into one
        // variant group; per-group variants keep file order (base row first).
        using MapType =
            spp::sparse_hash_map<char *, std::vector<Entry>, const_char_hash, const_char_equal>;
        using SppIterator = MapType::const_iterator;

        std::vector<char> filebuf;
        MapType           map;

        // Store/load a sparsepp const_iterator to/from the two void* slots (_row, _col).
        // Uses memcpy to safely round-trip without accessing sparsepp internal member names.
        static_assert(sizeof(SppIterator) <= 2 * sizeof(void *), "sparsepp iterator size exceeds two void* slots");

        static SppIterator loadIter(const void *row, const void *col) {
            SppIterator it{};
            const void *buf[2] = {row, col};
            std::memcpy(&it, buf, sizeof(it));
            return it;
        }

        static void storeIter(const SppIterator &it, const void *&row, const void *&col) {
            const void *buf[2] = {};
            std::memcpy(buf, &it, sizeof(it));
            row = buf[0];
            col = buf[1];
        }

        /// Index of the variant with the given suffix number within a group, or -1.
        static int findSuffixIndex(const std::vector<Entry> &variants, uint32_t suffix) {
            for (size_t i = 0; i < variants.size(); ++i) {
                if (variants[i].suffix == suffix)
                    return static_cast<int>(i);
            }
            return -1;
        }
    };

    PhonemeDict::PhonemeDict() : _impl(std::make_shared<Impl>()) {
    }

    PhonemeDict::~PhonemeDict() = default;

    void PhonemeDict::reset() {
        _impl = std::make_shared<Impl>();
    }

    bool PhonemeDict::load(const std::filesystem::path &path, std::error_code *ec) {
        if (ec)
            ec->clear();

        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            if (ec)
                *ec = make_last_error();
            return false;
        }

        stdc_impl_t;
        auto &filebuf = impl.filebuf;
        auto &map     = impl.map;

        file.seekg(0, std::ios::end);
        const std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Guard against tellg() failure (pipes, special files, stream errors).
        // A negative file_size would cause negative resize/read and OOB writes.
        if (file_size < 0) {
            if (ec)
                *ec = make_last_error();
            return false;
        }

        filebuf.resize(static_cast<size_t>(file_size) + 1); // +1 for terminator
        if (!file.read(filebuf.data(), file_size)) {
            if (ec)
                *ec = std::error_code(errno, std::system_category());
            filebuf.clear();
            return false;
        }

        // Strip a UTF-8 BOM if the dict file starts with EF BB BF. Without
        // this, the first key would carry the BOM bytes and never match or
        // merge with its "(n)" variants (several bundled dicts carry a BOM:
        // deu/fra/ita/por/rus/spa). Physically shifting the buffer keeps all
        // offsets consistent with impl.filebuf.data().
        size_t parsed_size = static_cast<size_t>(file_size);
        if (parsed_size >= 3 && static_cast<unsigned char>(filebuf[0]) == 0xEF &&
            static_cast<unsigned char>(filebuf[1]) == 0xBB &&
            static_cast<unsigned char>(filebuf[2]) == 0xBF) {
            std::memmove(filebuf.data(), filebuf.data() + 3, parsed_size - 3);
            parsed_size -= 3;
        }
        filebuf.resize(parsed_size + 1); // shrink to content + terminator slot
        filebuf[parsed_size] = '\n';     // add terminating line break
        map.clear();

        // Parse the buffer
        const auto buffer_begin = filebuf.data();
        const auto buffer_end   = buffer_begin + filebuf.size();

        // Estimate line numbers if the file is too large
        static constexpr size_t large_file_size = 1 * 1024 * 1024;
        if (parsed_size > large_file_size) {
            const size_t line_cnt = std::count(buffer_begin, buffer_end, '\n') + 1;
            map.reserve(line_cnt);
        }

        // Traverse lines
        {
            auto start = buffer_begin;
            while (start < buffer_end) {
                // Skip line breaks
                while (start < buffer_end && (*start == '\r' || *start == '\n')) {
                    *start = '\0';
                    start++;
                }
                if (start >= buffer_end)
                    break;

                uint32_t value_cnt = 0;

                // Find tab or line break
                auto p = start + 1;
                while (p < buffer_end && *p != '\t' && *p != '\r' && *p != '\n') {
                    ++p;
                }

                if (p >= buffer_end || *p == '\r' || *p == '\n') {
                    // Tab not found, skip to next line
                    start = p + 1;
                    continue;
                }

                // Tab found at p
                *p = '\0';
                ++p; // move past tab

                // Skip leading spaces after tab
                while (p < buffer_end && *p == ' ') {
                    *p = '\0';
                    ++p;
                }
                char *value_start = p;

                // Null-terminate all spaces and the line-ending newline/cr.
                // A compaction pass below removes empty strings caused by
                // consecutive/trailing spaces (consistent with
                // DirectS2P::convert which skips empty tokens).
                while (p < buffer_end && *p != '\r' && *p != '\n') {
                    if (*p == ' ') {
                        *p = '\0';
                    }
                    ++p;
                }
                if (p < buffer_end) {
                    *p = '\0'; // null-terminate the line ending
                }

                // Compact phoneme values: remove empty strings so the
                // PhonemeList iterator (which advances by strlen+1) never
                // returns spurious empty phonemes.
                {
                    char       *read            = value_start;
                    char       *write           = value_start;
                    const char *end             = (p < buffer_end) ? p : (buffer_end - 1);
                    uint32_t    compacted_count = 0;
                    while (read <= end) {
                        const size_t len = std::strlen(read);
                        if (len > 0) {
                            if (read != write) {
                                std::memmove(write, read, len + 1);
                            }
                            write += len + 1;
                            ++compacted_count;
                        }
                        read += len + 1;
                        if (read > end) {
                            break;
                        }
                    }
                    value_cnt = compacted_count;
                }

                // Strip a CMU-style "(n)" variant suffix from the key in place
                // (filebuf is mutable memory), so all rows of a multi-pronunciation
                // word merge under the bare base key. Keys without a strict
                // tail "(digits)" pattern are left untouched, keeping other
                // languages' dictionaries byte-for-byte compatible.
                uint32_t suffix = 0;
                {
                    const size_t key_len = std::strlen(start);
                    size_t       spos    = 0;
                    uint32_t     sno     = 0;
                    if (parse_variant_suffix(start, key_len, spos, sno)) {
                        start[spos] = '\0';
                        suffix      = sno;
                    }
                }

                // Merge into the base key's variant group (file order). Skip
                // exact duplicates (same suffix number AND identical phoneme
                // sequence) so repeated lines don't multiply candidates.
                auto &variants = map[start];
                Impl::Entry newEntry{static_cast<uint32_t>(value_start - buffer_begin), value_cnt,
                                     suffix};
                bool        dup = false;
                for (const auto &e : variants) {
                    if (e.suffix != suffix || e.count != value_cnt)
                        continue;
                    const char *a = buffer_begin + e.offset;
                    const char *b = value_start;
                    uint32_t    k = 0;
                    for (; k < value_cnt; ++k) {
                        if (std::strcmp(a, b) != 0)
                            break;
                        a += std::strlen(a) + 1;
                        b += std::strlen(b) + 1;
                    }
                    if (k == value_cnt) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    variants.push_back(newEntry);
                }
                start = p + 1;
            }
        }
        return true;
    }

    // ==================== Iterator ====================
    // Uses memcpy-based round-tripping of sparsepp iterators through the opaque
    // void* slots. This avoids direct access to sparsepp internal member names.

    void PhonemeDict::iterator::fetch() const {
        if (_copy) {
            return;
        }
        auto              it       = Impl::loadIter(_row, _col);
        const char       *key      = it->first;
        const auto       &variants = it->second;
        const size_t      idx =
            (_var >= 0 && static_cast<size_t>(_var) < variants.size()) ? static_cast<size_t>(_var) : 0;
        PhonemeList value(_buf + variants[idx].offset, variants[idx].count);
        _copy = std::make_pair(key, value);
    }

    void PhonemeDict::iterator::next() {
        auto it = Impl::loadIter(_row, _col);
        ++it;
        Impl::storeIter(it, _row, _col);
        _var = 0;
        _copy.reset();
    }

    void PhonemeDict::iterator::prev() {
        auto it = Impl::loadIter(_row, _col);
        --it;
        Impl::storeIter(it, _row, _col);
        _var = 0;
        _copy.reset();
    }

    bool PhonemeDict::iterator::equals(const iterator &RHS) const {
        auto lhs = Impl::loadIter(_row, _col);
        auto rhs = Impl::loadIter(RHS._row, RHS._col);
        return lhs == rhs;
    }

    PhonemeDict::iterator PhonemeDict::find(const char *key) const {
        stdc_impl_t;
        auto &map = impl.map;
        if (!key) {
            return end();
        }
        // const_cast is safe: sparsepp::sparse_hash_map::find() takes non-const key but does not modify it
        auto it = map.find(const_cast<char *>(key));

        int variantIndex = 0;
        if (it == map.end()) {
            // Suffix-aware fallback (D4): a suffixed key like "word(2)"
            // resolves against the merged base group and selects exactly the
            // variant with that suffix number. No candidates are involved.
            size_t   pos = 0;
            uint32_t no  = 0;
            if (!parse_variant_suffix(key, std::strlen(key), pos, no)) {
                return end();
            }
            const std::string base(key, pos);
            it = map.find(const_cast<char *>(base.c_str()));
            if (it == map.end()) {
                return end();
            }
            variantIndex = Impl::findSuffixIndex(it->second, no);
            if (variantIndex < 0) {
                return end();
            }
        }

        const void *row = nullptr;
        const void *col = nullptr;
        Impl::storeIter(it, row, col);
        return iterator(impl.filebuf.data(), row, col, variantIndex);
    }

    bool PhonemeDict::contains(const char *key) const {
        return find(key) != end();
    }

    PhonemeList PhonemeDict::operator[](const char *key) const {
        const auto it = find(key);
        if (it == end()) {
            return PhonemeList();
        }
        return it->second;
    }

    std::vector<PhonemeList> PhonemeDict::lookupAll(const char *key) const {
        stdc_impl_t;
        std::vector<PhonemeList> out;
        if (!key) {
            return out;
        }
        auto &map = impl.map;
        // const_cast is safe: sparsepp::sparse_hash_map::find() only reads, never modifies
        auto it = map.find(const_cast<char *>(key));

        if (it == map.end()) {
            // Suffix-aware fallback (D4): return only the matching variant,
            // never the whole group, so no candidate choice is implied.
            size_t   pos = 0;
            uint32_t no  = 0;
            if (!parse_variant_suffix(key, std::strlen(key), pos, no)) {
                return out;
            }
            const std::string base(key, pos);
            it = map.find(const_cast<char *>(base.c_str()));
            if (it == map.end()) {
                return out;
            }
            const int idx = Impl::findSuffixIndex(it->second, no);
            if (idx < 0) {
                return out;
            }
            const auto &e = it->second[static_cast<size_t>(idx)];
            // NOTE: construct the PhonemeList here and push a copy —
            // vector::emplace_back would invoke the protected constructor from
            // inside allocator_traits (not a friend) and fails on MSVC (C2672).
            out.push_back(PhonemeList(impl.filebuf.data() + e.offset, e.count));
            return out;
        }

        out.reserve(it->second.size());
        for (const auto &e : it->second) {
            out.push_back(PhonemeList(impl.filebuf.data() + e.offset, e.count));
        }
        return out;
    }

    bool PhonemeDict::empty() const {
        stdc_impl_t;
        return impl.map.empty();
    }

    size_t PhonemeDict::size() const {
        stdc_impl_t;
        return impl.map.size();
    }

    PhonemeDict::iterator PhonemeDict::begin() const {
        stdc_impl_t;
        const auto  it  = impl.map.begin();
        const void *row = nullptr;
        const void *col = nullptr;
        Impl::storeIter(it, row, col);
        return iterator(impl.filebuf.data(), row, col);
    }

    PhonemeDict::iterator PhonemeDict::end() const {
        stdc_impl_t;
        const auto  it  = impl.map.end();
        const void *row = nullptr;
        const void *col = nullptr;
        Impl::storeIter(it, row, col);
        return iterator(impl.filebuf.data(), row, col);
    }

} // namespace srt::g2p
