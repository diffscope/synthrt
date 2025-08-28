#include "PhonemeDict.h"

#include <fstream>

#include <sparsepp/spp.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

namespace ds {

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
        };
        std::vector<char> filebuf;
        spp::sparse_hash_map<char *, Entry, const_char_hash, const_char_equal> map;
    };

    PhonemeDict::PhonemeDict() : _impl(std::make_shared<Impl>()) {
    }

    PhonemeDict::~PhonemeDict() = default;

    bool PhonemeDict::load(const std::filesystem::path &path, std::error_code *ec) {
        if (ec)
            ec->clear();

        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            if (ec)
                *ec = make_last_error();
            return false;
        }

        __stdc_impl_t;
        auto &filebuf = impl.filebuf;
        auto &map = impl.map;

        file.seekg(0, std::ios::end);
        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        filebuf.resize(file_size + 1); // +1 for terminator
        if (!file.read(filebuf.data(), file_size)) {
            if (ec)
                *ec = std::error_code(errno, std::system_category());
            filebuf.clear();
            return false;
        }
        filebuf[file_size] = '\n'; // add terminating line break
        map.clear();

        // Parse the buffer
        const auto buffer_begin = filebuf.data();
        const auto buffer_end = buffer_begin + filebuf.size();

        // Estimate line numbers if the file is too large
        static constexpr const size_t larget_file_size = 1 * 1024 * 1024;
        if (file_size > larget_file_size) {
            size_t line_cnt = std::count(buffer_begin, buffer_end, '\n') + 1;
            map.reserve(line_cnt);
        }

        // Traverse lines
        {
            auto start = buffer_begin;
            while (start < buffer_end) {
                while (start < buffer_end && (*start == '\r' || *start == '\n')) {
                    *start = '\0';
                    start++;
                }

                char *value_start = nullptr;
                uint32_t value_cnt = 0;

                // Find tab
                auto p = start + 1;
                while (p < buffer_end) {
                    switch (*p) {
                        case '\t':
                            value_start = p + 1;
                            *p = '\0';
                            goto out_tab_find;

                        case '\r':
                        case '\n':
                            start = p + 1;
                            goto out_next_line;

                        default:
                            break;
                    }
                    ++p;
                }

                // Tab not found
                while (start < buffer_end && (*start != '\r' && *start != '\n')) {
                    *start = '\0';
                    start++;
                }
                goto out_next_line;

            out_tab_find:
                // Find space or line break
                while (p < buffer_end) {
                    switch (*p) {
                        case ' ':
                            value_cnt++;
                            *p = '\0';
                            break;

                        case '\r':
                        case '\n':
                            value_cnt++;
                            *p = '\0';
                            goto out_success;

                        default:
                            break;
                    }
                    ++p;
                }

            out_success: {
                map[start] = Impl::Entry{uint32_t(value_start - buffer_begin), value_cnt};
                start = p + 1;
            }
            out_next_line:;
            }
        }
        return true;
    }

    void PhonemeDict::iterator::fetch() const {
        if (_copy) {
            return;
        }
        auto it = decltype(Impl::map)::const_iterator();
        it.row_current = (decltype(it.row_current)) const_cast<void *>(_row);
        it.col_current = (decltype(it.col_current)) const_cast<void *>(_col);

        const char *key = it->first;
        PhonemeList value(_buf + it->second.offset, it->second.count);

        _copy = std::make_pair(key, value);
    }

    void PhonemeDict::iterator::next() {
        auto it = decltype(Impl::map)::const_iterator();
        it.row_current = (decltype(it.row_current)) _row;
        it.col_current = (decltype(it.col_current)) _col;
        ++it;
        _row = it.row_current;
        _col = it.col_current;
        _copy.reset();
    }

    void PhonemeDict::iterator::prev() {
        auto it = decltype(Impl::map)::const_iterator();
        it.row_current = (decltype(it.row_current)) _row;
        it.col_current = (decltype(it.col_current)) _col;
        --it;
        _row = it.row_current;
        _col = it.col_current;
        _copy.reset();
    }

    bool PhonemeDict::iterator::equals(const iterator &RHS) const {
        auto it = decltype(Impl::map)::const_iterator();
        it.row_current = (decltype(it.row_current)) _row;
        it.col_current = (decltype(it.col_current)) _col;
        auto it2 = decltype(Impl::map)::const_iterator();
        it2.row_current = (decltype(it2.row_current)) RHS._row;
        it2.col_current = (decltype(it2.col_current)) RHS._col;
        return it == it2;
    }

    PhonemeDict::iterator PhonemeDict::find(const char *key) const {
        __stdc_impl_t;
        auto &filebuf = impl.filebuf;
        auto &map = impl.map;
        if (!key) {
            return end();
        }
        auto it = map.find(const_cast<char *>(key));
        if (it == map.end()) {
            return end();
        }
        return iterator(impl.filebuf.data(), it.row_current, it.col_current);
    }

    bool PhonemeDict::contains(const char *key) const {
        __stdc_impl_t;
        auto &map = impl.map;
        if (!key) {
            return false;
        }
        return map.find(const_cast<char *>(key)) != map.end();
    }

    PhonemeList PhonemeDict::operator[](const char *key) const {
        __stdc_impl_t;
        auto &map = impl.map;
        if (!key) {
            return PhonemeList();
        }
        auto it = map.find(const_cast<char *>(key));
        if (it == map.end()) {
            return PhonemeList();
        }
        return PhonemeList(impl.filebuf.data() + it->second.offset, it->second.count);
    }

    bool PhonemeDict::empty() const {
        __stdc_impl_t;
        return impl.map.empty();
    }

    size_t PhonemeDict::size() const {
        __stdc_impl_t;
        return impl.map.size();
    }

    PhonemeDict::iterator PhonemeDict::begin() const {
        __stdc_impl_t;
        auto it = impl.map.begin();
        return iterator(impl.filebuf.data(), it.row_current, it.col_current);
    }

    PhonemeDict::iterator PhonemeDict::end() const {
        __stdc_impl_t;
        auto it = impl.map.end();
        return iterator(impl.filebuf.data(), it.row_current, it.col_current);
    }

}