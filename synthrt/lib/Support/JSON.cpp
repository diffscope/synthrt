#include "JSON.h"

#include <nlohmann/json.hpp>

namespace {

    using namespace srt;

    struct EmptyValues {
        static inline const JsonValue &nullValue() {
            static const JsonValue null = JsonValue(JsonValue::Null);
            return null;
        }
        static inline const JsonValue &undefinedValue() {
            static const JsonValue false_ = JsonValue(JsonValue::Undefined);
            return false_;
        }
        static inline const JsonArray &emptyArray() {
            static const JsonArray emptyArray;
            return emptyArray;
        }
        static inline const JsonObject &emptyObject() {
            static const JsonObject emptyObject;
            return emptyObject;
        }
    };

    /// Proxy container declaration
    template <class T, class... _Mods>
    class proxy_vector {
    public:
        using _Vec = std::vector<T, _Mods...>;
        using _Buf = std::vector<JsonValue>;

        _Buf buf;

    public:
        using value_type = T;
        using allocator_type = typename _Vec::allocator_type;
        using pointer = typename _Vec::pointer;
        using const_pointer = typename _Vec::const_pointer;
        using reference = T &;
        using const_reference = const T &;
        using size_type = typename _Vec::size_type;
        using difference_type = typename _Vec::difference_type;

        template <bool _Const>
        class iterator_base {
        public:
            using _It = std::conditional_t<_Const, _Buf::const_iterator, _Buf::iterator>;

            _It it;

        public:
            using iterator_category = typename _It::iterator_category;
            using value_type = T;
            using difference_type = typename _It::difference_type;
            using pointer = std::conditional_t<_Const, const T *, T *>;
            using reference = std::conditional_t<_Const, const T &, T &>;

            iterator_base() noexcept {
            }

            iterator_base(_It it) noexcept : it(it) {
            }

            reference operator*() const noexcept;
            pointer operator->() const noexcept;

            iterator_base &operator++() noexcept {
                ++it;
                return *this;
            }
            iterator_base operator++(int) noexcept {
                auto tmp = *this;
                ++(*this);
                return tmp;
            }
            iterator_base &operator--() noexcept {
                --it;
                return *this;
            }
            iterator_base operator--(int) noexcept {
                auto tmp = *this;
                --(*this);
                return tmp;
            }
            iterator_base &operator+=(const difference_type &n) noexcept {
                it += n;
                return *this;
            }
            iterator_base operator+(const difference_type &n) const noexcept {
                auto tmp = *this;
                tmp += n;
                return tmp;
            }
            iterator_base &operator-=(const difference_type &n) noexcept {
                it -= n;
                return *this;
            }
            iterator_base operator-(const difference_type &n) const noexcept {
                auto tmp = *this;
                tmp -= n;
                return tmp;
            }
            difference_type operator-(const iterator_base &RHS) const noexcept {
                return it - RHS.it;
            }
            reference operator[](const difference_type &n) const noexcept;
            bool operator==(const iterator_base &RHS) const noexcept {
                return it == RHS.it;
            }
            bool operator!=(const iterator_base &RHS) const noexcept {
                return it != RHS.it;
            }
            bool operator<(const iterator_base &RHS) const noexcept {
                return it < RHS.it;
            }
            bool operator<=(const iterator_base &RHS) const noexcept {
                return it <= RHS.it;
            }
            bool operator>(const iterator_base &RHS) const noexcept {
                return it > RHS.it;
            }
            bool operator>=(const iterator_base &RHS) const noexcept {
                return it >= RHS.it;
            }
        };

        using iterator = iterator_base<false>;
        using const_iterator = iterator_base<true>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        proxy_vector() {
        }
        template <class _InputIterator>
        proxy_vector(_InputIterator first, _InputIterator last) {
            for (; first != last; ++first) {
                push_back(*first);
            }
        }

        iterator begin() noexcept {
            return buf.begin();
        }
        const_iterator begin() const noexcept {
            return buf.begin();
        }
        const_iterator cbegin() const noexcept {
            return buf.cbegin();
        }
        iterator end() noexcept {
            return buf.end();
        }
        const_iterator end() const noexcept {
            return buf.end();
        }
        const_iterator cend() const noexcept {
            return buf.cend();
        }
        reverse_iterator rbegin() noexcept {
            return buf.rbegin();
        }
        const_reverse_iterator rbegin() const noexcept {
            return buf.rbegin();
        }
        const_reverse_iterator crbegin() const noexcept {
            return buf.crbegin();
        }
        reverse_iterator rend() noexcept {
            return buf.rend();
        }
        const_reverse_iterator rend() const noexcept {
            return buf.rend();
        }
        const_reverse_iterator crend() const noexcept {
            return buf.crend();
        }
        bool empty() const noexcept {
            return buf.empty();
        }
        size_type size() const noexcept {
            return buf.size();
        }
        size_type max_size() const noexcept {
            return buf.max_size();
        }
        size_type capacity() const noexcept {
            return buf.capacity();
        }
        void reserve(size_type n) {
            buf.reserve(n);
        }
        void shrink_to_fit() {
            buf.shrink_to_fit();
        }
        void clear() {
            buf.clear();
        }

        T &front();
        const T &front() const;
        T &back();
        const T &back() const;

        void insert(const_iterator pos, const T &value);
        void insert(const_iterator pos, T &&value);
        void push_back(const T &value);
        void push_back(T &&value);
        template <class... Args>
        reference emplace_back(Args &&...args);

        iterator erase(iterator pos) {
            return buf.erase(pos.it);
        }
        iterator erase(const_iterator pos) {
            return buf.erase(pos.it);
        }
        iterator erase(iterator first, iterator last) {
            return buf.erase(first.it, last.it);
        }
        iterator erase(const_iterator first, const_iterator last) {
            return buf.erase(first.it, last.it);
        }

        void pop_back() {
            buf.pop_back();
        }
        void resize(size_type count) {
            buf.resize(count);
        }
        void swap(proxy_vector &RHS) noexcept {
            buf.swap(RHS.buf);
        }

        bool operator==(const proxy_vector &RHS) const noexcept {
            return buf == RHS.buf;
        }
        bool operator!=(const proxy_vector &RHS) const {
            return !(*this == RHS);
        }
    };


    template <class Key, class T, class... _Mods>
    class proxy_map {
    public:
        using _Map = std::map<Key, T, _Mods...>;
        using _Buf = std::map<std::string, JsonValue>;

        _Buf buf;

    public:
        using key_type = Key;
        using mapped_type = T;
        using key_compare = typename _Map::key_compare;
        using value_compare = typename _Map::value_compare;
        using value_type = typename _Map::value_type;
        using allocator_type = typename _Map::allocator_type;
        using size_type = typename _Map::size_type;
        using difference_type = typename _Map::difference_type;
        using pointer = typename _Map::pointer;
        using const_pointer = typename _Map::const_pointer;
        using reference = value_type &;
        using const_reference = const value_type &;


        template <bool _Const>
        class iterator_base {
        public:
            using _It = std::conditional_t<_Const, _Buf::const_iterator, _Buf::iterator>;
            using _Ty = std::conditional_t<_Const, const T, T>;

            // pretend to be a reference to an std::pair<const std::string, T>
            struct _Ref {
                const std::string &first;
                _Ty &second;

                _Ref(const std::string &first, _Ty &second) : first(first), second(second) {
                }

                operator std::pair<const std::string, T>() const {
                    return std::pair<const std::string, T>(first, second);
                }
            };

            _It it;
            mutable std::optional<_Ref> ref;

        public:
            using iterator_category = typename _It::iterator_category;
            using value_type = _Ref;
            using difference_type = typename _It::difference_type;
            using pointer = std::conditional_t<_Const, const _Ref *, _Ref *>;
            using reference = std::conditional_t<_Const, const _Ref &, _Ref &>;

            iterator_base() noexcept {
            }

            iterator_base(_It it) noexcept : it(it) {
            }

            iterator_base(const iterator_base &RHS) : it(RHS.it) {
                ref.emplace(RHS.ref->first, RHS.ref->second);
            }

            iterator_base &operator=(const iterator_base &RHS) {
                if (this != &RHS) {
                    it = RHS.it;
                    ref.emplace(RHS.ref->first, RHS.ref->second);
                }
                return *this;
            }

            reference operator*() const noexcept;
            pointer operator->() const noexcept;

            iterator_base &operator++() noexcept {
                ++it;
                return *this;
            }
            iterator_base operator++(int) noexcept {
                auto tmp = *this;
                ++(*this);
                return tmp;
            }
            iterator_base &operator--() noexcept {
                --it;
                return *this;
            }
            iterator_base operator--(int) noexcept {
                auto tmp = *this;
                --(*this);
                return tmp;
            }

            reference operator[](const difference_type &n) const noexcept;

            bool operator==(const iterator_base &RHS) const noexcept {
                return it == RHS.it;
            }
            bool operator!=(const iterator_base &RHS) const noexcept {
                return it != RHS.it;
            }
        };

        using iterator = iterator_base<false>;
        using const_iterator = iterator_base<true>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // since c++17
        using insert_return_type = typename _Map::insert_return_type;

        proxy_map() {
        }
        template <class _InputIterator>
        proxy_map(_InputIterator first, _InputIterator last) {
            for (; first != last; ++first) {
                insert(*first);
            }
        }

        T &at(const Key &key);
        const T &at(const Key &key) const;
        T &operator[](const Key &key);
        T &operator[](Key &&key);

        iterator begin() noexcept {
            return buf.begin();
        }
        const_iterator begin() const noexcept {
            return buf.begin();
        }
        const_iterator cbegin() const noexcept {
            return begin();
        }
        iterator end() noexcept {
            return buf.end();
        }
        const_iterator end() const noexcept {
            return buf.end();
        }
        const_iterator cend() const noexcept {
            return buf.cend();
        }
        reverse_iterator rbegin() noexcept {
            return buf.rbegin();
        }
        const_reverse_iterator rbegin() const noexcept {
            return buf.rbegin();
        }
        const_reverse_iterator crbegin() const noexcept {
            return buf.crbegin();
        }
        reverse_iterator rend() noexcept {
            return buf.rend();
        }
        const_reverse_iterator rend() const noexcept {
            return buf.rend();
        }
        const_reverse_iterator crend() const noexcept {
            return buf.crend();
        }

        bool empty() const noexcept {
            return buf.empty();
        }
        size_type size() const noexcept {
            return buf.size();
        }
        size_type max_size() const noexcept {
            return buf.max_size();
        }
        void clear() {
            buf.clear();
        }

        std::pair<iterator, bool> insert(const value_type &value);
        iterator insert(iterator pos, const value_type &value);
        iterator insert(const_iterator pos, const value_type &value);

        iterator erase(iterator pos) {
            return buf.erase(pos.it);
        }
        iterator erase(const_iterator pos) {
            return erase(pos.it);
        }
        size_type erase(const Key &key) {
            return buf.erase(key);
        }
        size_type count(const Key &key) const {
            return buf.count(key);
        }
        iterator find(const Key &key) {
            return buf.find(key);
        }
        const_iterator find(const Key &key) const {
            return buf.find(key);
        }
        void swap(proxy_map &RHS) noexcept {
            buf.swap(RHS.buf);
        }

        bool operator==(const proxy_map &RHS) const {
            return buf == RHS.buf;
        }
        bool operator!=(const proxy_map &RHS) const {
            return !(*this == RHS);
        }
    };

    using JSON = nlohmann::basic_json<proxy_map, proxy_vector, std::string>;
}

namespace srt {

#ifndef SYNTHRT_JSON_IN_PLACE
    class JsonValueContainer {
    public:
        JSON json;
    };
#endif

}

namespace {

    class JV : public JsonValue {
    public:
        inline JV(void *p, bool move) : JsonValue(p, move) {
        }

        static inline void construct(JsonValue &val) {
            auto &jv = static_cast<JV &>(val);
#ifdef SYNTHRT_JSON_IN_PLACE
            new (&jv.buf.p) JSON();
#else
            jv.c = std::make_shared<JsonValueContainer>();
#endif
        }

        static inline void construct(JsonValue &val, const JsonValue &RHS) {
            auto &jv = static_cast<JV &>(val);
#ifdef SYNTHRT_JSON_IN_PLACE
            new (&jv.buf.p) JSON(JV::unpack(RHS));
#else
            jv.c = static_cast<const JV &>(RHS).c;
#endif
        }

        static inline void construct(JsonValue &val, JsonValue &&RHS) {
            auto &jv = static_cast<JV &>(val);
#ifdef SYNTHRT_JSON_IN_PLACE
            new (&jv.buf.p) JSON(std::move(JV::unpack(RHS)));
#else
            jv.c = static_cast<const JV &>(RHS).c;
#endif
        }

        static inline void construct(JsonValue &val, void *raw, bool move) {
            auto &jv = static_cast<JV &>(val);
#ifdef SYNTHRT_JSON_IN_PLACE
            if (move) {
                new (&jv.buf.p) JSON(std::move(*reinterpret_cast<JSON *>(raw)));
            } else {
                new (&jv.buf.p) JSON(*reinterpret_cast<JSON *>(raw));
            }
#else
            jv.c = std::make_shared<JsonValueContainer>();
            if (move) {
                jv.c->json = JSON(std::move(*reinterpret_cast<JSON *>(raw)));
            } else {
                jv.c->json = JSON(*reinterpret_cast<JSON *>(raw));
            }
#endif
        }

        static inline void destruct(JsonValue &val) {
#ifdef SYNTHRT_JSON_IN_PLACE
            unpack(val).~basic_json();
#endif
        }

        static inline JSON &unpack(JsonValue &val) {
            auto &jv = static_cast<JV &>(val);
#ifdef SYNTHRT_JSON_IN_PLACE
            return *reinterpret_cast<JSON *>(&jv.buf.p);
#else
            return jv.c->json;
#endif
        }

        static inline const JSON &unpack(const JsonValue &val) {
            auto &jv = static_cast<const JV &>(val);
#ifdef SYNTHRT_JSON_IN_PLACE
            return *reinterpret_cast<const JSON *>(&jv.buf.p);
#else
            return jv.c->json;
#endif
        }

        static inline JsonValue pack(const JSON &json) {
            return JV(const_cast<JSON *>(&json), false);
        }

        static inline JsonValue pack(JSON &&json) {
            return JV(&json, true);
        }
    };

    // proxy container implementations
    template <class T, class... _Mods>
    template <bool _Const>
    typename proxy_vector<T, _Mods...>::template iterator_base<_Const>::reference
        proxy_vector<T, _Mods...>::iterator_base<_Const>::operator*() const noexcept {
        return JV::unpack(*it);
    }
    template <class T, class... _Mods>
    template <bool _Const>
    typename proxy_vector<T, _Mods...>::template iterator_base<_Const>::pointer
        proxy_vector<T, _Mods...>::iterator_base<_Const>::operator->() const noexcept {
        return &JV::unpack(*it);
    }

    template <class T, class... _Mods>
    template <bool _Const>
    typename proxy_vector<T, _Mods...>::template iterator_base<_Const>::reference
        proxy_vector<T, _Mods...>::iterator_base<_Const>::operator[](
            const difference_type &n) const noexcept {
        return JV::unpack(it[n]);
    }


    template <class T, class... _Mods>
    T &proxy_vector<T, _Mods...>::front() {
        return JV::unpack(buf.front());
    }
    template <class T, class... _Mods>
    const T &proxy_vector<T, _Mods...>::front() const {
        return JV::unpack(buf.front());
    }
    template <class T, class... _Mods>
    T &proxy_vector<T, _Mods...>::back() {
        return JV::unpack(buf.back());
    }
    template <class T, class... _Mods>
    const T &proxy_vector<T, _Mods...>::back() const {
        return JV::unpack(buf.back());
    }

    template <class T, class... _Mods>
    void proxy_vector<T, _Mods...>::insert(const_iterator pos, const T &value) {
        buf.insert(pos.it, JV::pack(value));
    }
    template <class T, class... _Mods>
    void proxy_vector<T, _Mods...>::insert(const_iterator pos, T &&value) {
        buf.insert(pos.it, JV::pack(std::move(value)));
    }
    template <class T, class... _Mods>
    void proxy_vector<T, _Mods...>::push_back(const T &value) {
        buf.push_back(JV::pack(value));
    }
    template <class T, class... _Mods>
    void proxy_vector<T, _Mods...>::push_back(T &&value) {
        buf.push_back(JV::pack(std::move(value)));
    }
    template <class T, class... _Mods>
    template <class... Args>
    typename proxy_vector<T, _Mods...>::reference
        proxy_vector<T, _Mods...>::emplace_back(Args &&...args) {
        return JV::unpack(buf.emplace_back(JV::pack(T(std::forward<Args>(args)...))));
    }




    template <class Key, class T, class... _Mods>
    template <bool _Const>
    typename proxy_map<Key, T, _Mods...>::template iterator_base<_Const>::reference
        proxy_map<Key, T, _Mods...>::iterator_base<_Const>::operator*() const noexcept {
        ref.emplace(it->first, JV::unpack(it->second));
        return ref.value();
    }
    template <class Key, class T, class... _Mods>
    template <bool _Const>
    typename proxy_map<Key, T, _Mods...>::template iterator_base<_Const>::pointer
        proxy_map<Key, T, _Mods...>::iterator_base<_Const>::operator->() const noexcept {
        ref.emplace(it->first, JV::unpack(it->second));
        return &ref.value();
    }
    template <class Key, class T, class... _Mods>
    template <bool _Const>
    typename proxy_map<Key, T, _Mods...>::template iterator_base<_Const>::reference
        proxy_map<Key, T, _Mods...>::iterator_base<_Const>::operator[](
            const difference_type &n) const noexcept {
        auto &tmp = it[n];
        ref.emplace(tmp.first, JV::unpack(tmp.second));
        return ref.value();
    }

    template <class Key, class T, class... _Mods>
    const T &proxy_map<Key, T, _Mods...>::at(const Key &key) const {
        return JV::unpack(buf.at(key));
    }
    template <class Key, class T, class... _Mods>
    T &proxy_map<Key, T, _Mods...>::operator[](const Key &key) {
        return JV::unpack(buf[key]);
    }
    template <class Key, class T, class... _Mods>
    T &proxy_map<Key, T, _Mods...>::operator[](Key &&key) {
        return JV::unpack(buf[std::move(key)]);
    }

    template <class Key, class T, class... _Mods>
    std::pair<typename proxy_map<Key, T, _Mods...>::iterator, bool>
        proxy_map<Key, T, _Mods...>::insert(const value_type &value) {
        auto tmp = buf.insert(std::make_pair(value.first, JV::pack(value.second)));
        return std::make_pair(iterator(tmp.first), tmp.second);
    }

    template <class Key, class T, class... _Mods>
    typename proxy_map<Key, T, _Mods...>::iterator
        proxy_map<Key, T, _Mods...>::insert(iterator pos, const value_type &value) {
        return buf.insert(pos.it, std::make_pair(value.first, JV::pack(value.second)));
    }

    template <class Key, class T, class... _Mods>
    typename proxy_map<Key, T, _Mods...>::iterator
        proxy_map<Key, T, _Mods...>::insert(const_iterator pos, const value_type &value) {
        return buf.insert(pos.it, std::make_pair(value.first, JV::pack(value.second)));
    }

}

namespace srt {

    JsonValue::JsonValue(Type type) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        switch (type) {
            case Null: {
                json = nullptr;
                break;
            }
            case Bool: {
                json = false;
                break;
            }
            case Int: {
                json = int64_t(0);
                break;
            }
            case UInt: {
                json = uint64_t(0);
                break;
            }
            case Double: {
                json = 0.0;
                break;
            }
            case String: {
                json = std::string();
                break;
            }
            case Binary:
                json = JSON::binary_t();
                break;
            case Array: {
                json = JSON::array_t();
                break;
            }
            case Object: {
                json = JSON::object_t();
                break;
            }
            case Undefined: {
                json = nullptr;
                break;
            }
        }
    }

    JsonValue::JsonValue(bool b) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        json = b;
    }

    JsonValue::JsonValue(double d) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        json = d;
    }

    JsonValue::JsonValue(int64_t i) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        json = i;
    }

    JsonValue::JsonValue(uint64_t u) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        json = u;
    }

    JsonValue::JsonValue(std::string_view s) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        json = s;
    }

    JsonValue::JsonValue(stdc::array_view<uint8_t> bytes) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        json = JSON::binary_t(bytes.vec());
    }

    JsonValue::JsonValue(const JsonArray &a) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        JSON::array_t arr;
        arr.buf = a;
        json = std::move(arr);
    }

    JsonValue::JsonValue(JsonArray &&a) noexcept {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        JSON::array_t arr;
        arr.buf = std::move(a);
        json = std::move(arr);
    }

    JsonValue::JsonValue(const JsonObject &o) {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        JSON::object_t obj;
        obj.buf = o;
        json = std::move(obj);
    }

    JsonValue::JsonValue(JsonObject &&o) noexcept {
        JV::construct(*this);
        auto &json = JV::unpack(*this);
        JSON::object_t obj;
        obj.buf = std::move(o);
        json = std::move(obj);
    }

    JsonValue::~JsonValue() {
        JV::destruct(*this);
    }

    JsonValue::JsonValue(const JsonValue &RHS) {
        JV::construct(*this, RHS);
    }

    JsonValue::JsonValue(JsonValue &&RHS) noexcept {
        JV::construct(*this, std::move(RHS));
    }

    JsonValue &JsonValue::operator=(const JsonValue &RHS) {
        if (this != &RHS) {
            JV::unpack(*this) = JV::unpack(RHS);
        }
        return *this;
    }

    void JsonValue::swap(JsonValue &RHS) noexcept {
        if (this != &RHS) {
            JV::unpack(*this).swap(JV::unpack(RHS));
        }
    }

    JsonValue::Type JsonValue::type() const {
        auto &json = JV::unpack(*this);
        JsonValue::Type type = Undefined;
        switch (json.type()) {
            case nlohmann::detail::value_t::null:
                type = Null;
                break;
            case nlohmann::detail::value_t::object:
                type = Object;
                break;
            case nlohmann::detail::value_t::array:
                type = Array;
                break;
            case nlohmann::detail::value_t::string:
                type = String;
                break;
            case nlohmann::detail::value_t::boolean:
                type = Bool;
                break;
            case nlohmann::detail::value_t::number_integer:
                type = Int;
                break;
            case nlohmann::detail::value_t::number_unsigned:
                type = UInt;
                break;
            case nlohmann::detail::value_t::number_float:
                type = Double;
                break;
            case nlohmann::detail::value_t::binary:
                type = Binary;
                break;
            default:
                break;
        }
        return type;
    }
    bool JsonValue::toBool(bool defaultValue) const {
        auto &json = JV::unpack(*this);
        if (json.is_boolean()) {
            return json.get<bool>();
        }
        return defaultValue;
    }
    int64_t JsonValue::toInt(int64_t defaultValue) const {
        auto &json = JV::unpack(*this);
        switch (json.type()) {
            case nlohmann::detail::value_t::number_integer:
                return json.get<int64_t>();
            case nlohmann::detail::value_t::number_unsigned:
                return json.get<uint64_t>();
            case nlohmann::detail::value_t::number_float:
                return json.get<double>();
            default:
                break;
        }
        return defaultValue;
    }
    uint64_t JsonValue::toUInt(uint64_t defaultValue) const {
        auto &json = JV::unpack(*this);
        switch (json.type()) {
            case nlohmann::detail::value_t::number_integer:
                return json.get<int64_t>();
            case nlohmann::detail::value_t::number_unsigned:
                return json.get<uint64_t>();
            case nlohmann::detail::value_t::number_float:
                return json.get<double>();
            default:
                break;
        }
        return defaultValue;
    }
    double JsonValue::toDouble(double defaultValue) const {
        auto &json = JV::unpack(*this);
        switch (json.type()) {
            case nlohmann::detail::value_t::number_integer:
                return json.get<int64_t>();
            case nlohmann::detail::value_t::number_unsigned:
                return json.get<uint64_t>();
            case nlohmann::detail::value_t::number_float:
                return json.get<double>();
            default:
                break;
        }
        return defaultValue;
    }
    std::string_view JsonValue::toString(std::string_view defaultValue) const {
        auto &json = JV::unpack(*this);
        if (json.is_string()) {
            return json.get_ref<const std::string &>();
        }
        return defaultValue;
    }
    stdc::array_view<uint8_t> JsonValue::toBinary(stdc::array_view<uint8_t> defaultValue) const {
        auto &json = JV::unpack(*this);
        if (json.is_binary()) {
            auto &bin = json.get_binary();
            return stdc::array_view<uint8_t>(bin.data(), bin.size());
        }
        return defaultValue;
    }

    const JsonArray &JsonValue::toArray() const {
        auto &json = JV::unpack(*this);
        if (json.is_array()) {
            return json.get_ref<const JSON::array_t &>().buf;
        }
        return EmptyValues::emptyArray();
    }

    const JsonArray &JsonValue::toArray(const JsonArray &defaultValue) const {
        auto &json = JV::unpack(*this);
        if (json.is_array()) {
            return json.get_ref<const JSON::array_t &>().buf;
        }
        return defaultValue;
    }

    const JsonObject &JsonValue::toObject() const {
        auto &json = JV::unpack(*this);
        if (json.is_object()) {
            return json.get_ref<const JSON::object_t &>().buf;
        }
        return EmptyValues::emptyObject();
    }

    const JsonObject &JsonValue::toObject(const JsonObject &defaultValue) const {
        auto &json = JV::unpack(*this);
        if (json.is_object()) {
            return json.get_ref<const JSON::object_t &>().buf;
        }
        return defaultValue;
    }

    const JsonValue &JsonValue::operator[](std::string_view key) const {
        auto &json = JV::unpack(*this);
        if (json.is_object()) {
            auto &obj = json.get_ref<const JSON::object_t &>().buf;
            auto it = obj.find(std::string(key));
            if (it != obj.end()) {
                return it->second;
            }
        }
        return EmptyValues::undefinedValue();
    }

    const JsonValue &JsonValue::operator[](size_t i) const {
        auto &json = JV::unpack(*this);
        if (json.is_array()) {
            auto &arr = json.get_ref<const JSON::array_t &>().buf;
            if (i < arr.size()) {
                return arr[i];
            }
        }
        return EmptyValues::undefinedValue();
    }

    bool JsonValue::operator==(const JsonValue &RHS) const {
        return JV::unpack(*this) == JV::unpack(RHS);
    }

    std::string JsonValue::toJson(int indent) const {
        return JV::unpack(*this).dump(indent);
    }
    JsonValue JsonValue::fromJson(std::string_view json, bool ignoreComments, std::string *error) {
        JsonValue val;
        try {
            auto jv = JSON::parse(json, nullptr, true, ignoreComments);
            val = JV::pack(std::move(jv));
        } catch (const std::exception &e) {
            if (error)
                *error = e.what();
        }
        return val;
    }

    std::vector<uint8_t> JsonValue::toCbor() const {
        return JSON::to_cbor(JV::unpack(*this));
    }

    JsonValue JsonValue::fromCbor(stdc::array_view<uint8_t> cbor, std::string *error) {
        JsonValue val;
        try {
            auto jv = JSON::from_cbor(cbor, false, true);
            val = JV::pack(std::move(jv));
        } catch (const std::exception &e) {
            if (error)
                *error = e.what();
        }
        return val;
    }

    /*!
        \internal
    */
    JsonValue::JsonValue(void *raw, bool move) {
        JV::construct(*this, raw, move);
    }

}