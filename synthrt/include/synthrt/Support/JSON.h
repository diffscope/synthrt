#ifndef SYNTHRT_JSON_H
#define SYNTHRT_JSON_H

#include <string>
#include <string_view>
#include <cstring>
#include <map>
#include <vector>
#include <memory>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/synthrt_global.h>

namespace srt {

    class JsonValue;

    using JsonArray = std::vector<JsonValue>;

    using JsonObject = std::map<std::string, JsonValue>;

    class JsonValueContainer;

    /// JsonValue - Encapsulates the \c nlohmann_json library to provide a refined and unified
    /// interface for JSON operations.
    class SYNTHRT_EXPORT JsonValue {
    public:
        enum Type {
            Null = 0,
            Bool,
            Double,
            Int,
            UInt,
            String,
            Binary,
            Array,
            Object,
            Undefined = 0x80,
        };

        JsonValue(Type = Null);
        JsonValue(bool b);
        JsonValue(double d);
        inline JsonValue(int i) : JsonValue(int64_t(i)) {
        }
        inline JsonValue(uint32_t i) : JsonValue(uint64_t(i)) {
        }
        JsonValue(int64_t i);
        JsonValue(uint64_t u);
        JsonValue(std::string_view s);
        inline JsonValue(const char *s, int size = -1)
            : JsonValue(size < 0 ? std::string_view(s) : std::string_view(s, size)) {
        }
        JsonValue(stdc::array_view<uint8_t> bytes);
        inline JsonValue(const uint8_t *data, int size)
            : JsonValue(stdc::array_view<uint8_t>(data, size)) {
        }
        JsonValue(const JsonArray &a);
        JsonValue(JsonArray &&a) noexcept;
        JsonValue(const JsonObject &o);
        JsonValue(JsonObject &&o) noexcept;
        ~JsonValue();

        JsonValue(const JsonValue &RHS);
        JsonValue(JsonValue &&RHS) noexcept;
        JsonValue &operator=(const JsonValue &RHS);
        inline JsonValue &operator=(JsonValue &&RHS) noexcept {
            swap(RHS);
            return *this;
        }

        void swap(JsonValue &RHS) noexcept;

    public:
        Type type() const;
        inline bool isNull() const {
            return type() == Null;
        }
        inline bool isBool() const {
            return type() == Bool;
        }
        inline bool isDouble() const {
            return type() == Double;
        }
        inline bool isString() const {
            return type() == String;
        }
        inline bool isArray() const {
            return type() == Array;
        }
        inline bool isObject() const {
            return type() == Object;
        }
        inline bool isUndefined() const {
            return type() == Undefined;
        }

        bool toBool(bool defaultValue = false) const;
        double toDouble(double defaultValue = 0) const;
        int64_t toInt(int64_t defaultValue = 0) const;
        uint64_t toUInt(uint64_t defaultValue = 0) const;
        std::string_view toStringView(std::string_view defaultValue = {}) const;
        const std::string &toString(const std::string &defaultValue = {}) const;
        stdc::array_view<uint8_t> toBinaryView(stdc::array_view<uint8_t> defaultValue = {}) const;
        const std::vector<uint8_t> &toBinary(const std::vector<uint8_t> &defaultValue = {}) const;
        const JsonArray &toArray() const;
        const JsonArray &toArray(const JsonArray &defaultValue) const;
        inline JsonArray toArray(JsonArray &&defaultValue) {
            return toArray(defaultValue);
        }
        const JsonObject &toObject() const;
        const JsonObject &toObject(const JsonObject &defaultValue) const;
        inline JsonObject toObject(JsonObject &&defaultValue) {
            return toObject(defaultValue);
        }

        const JsonValue &operator[](std::string_view key) const;
        const JsonValue &operator[](size_t i) const;

        bool operator==(const JsonValue &RHS) const;
        inline bool operator!=(const JsonValue &RHS) const {
            return !(*this == RHS);
        }

    public:
        /// Returns the serialized JSON text of this value.
        ///
        /// \param indent The number of spaces to indent the JSON text. If negative, no indentation
        /// is performed.
        std::string toJson(int indent = -1) const;

        /// Returns the serialized JsonValue instance of the given JSON text.
        ///
        /// \param ignoreComments Whether comments should be ignored and treated like whitespace
        /// (true) or yield a parse error (false)
        static JsonValue fromJson(std::string_view json, bool ignoreComments,
                                  std::string *error = nullptr);

        std::vector<uint8_t> toCbor() const;
        static JsonValue fromCbor(stdc::array_view<uint8_t> cbor, std::string *error = nullptr);

    protected:
        JsonValue(void *raw, bool move);

#ifdef SYNTHRT_JSON_IN_PLACE
        union {
            struct {
                uint8_t type;
                void *data;
                void *padding;
            } data;
            void *p;
        } buf;
#else
        std::shared_ptr<JsonValueContainer> c;
#endif
    };

}

#endif // SYNTHRT_JSON_H